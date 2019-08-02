import warnings
import numpy as np
from . import _alm as alm


optimizer_control_keys = ('linear_model',
                          'use_sparse_solver',
                          'maxnum_iteration',
                          'tolerance_iteration',
                          'output_frequency',
                          'standardize',
                          'displacement_normalization_factor',
                          'debiase_after_l1opt',
                          'cross_validation',
                          'l1_alpha',
                          'l1_alpha_min',
                          'l1_alpha_max',
                          'num_l1_alpha',
                          'l1_ratio',
                          'save_solution_path')


class ALM:
    """Calculate harmonic and anharmonic interatomic force constants

    Attributes
    ----------
    lavec : ndarray
        Basis vectors. a, b, c are given as column vectors.
        shape=(3, 3), dtype='double'
    xcoord : ndarray
        Fractional coordinates of atomic points.
        shape=(num_atoms, 3), dtype='double'
    numbers : ndarray
        Atomic numbers.
        shape=(num_atoms,), dtype='intc'
    kind_indices : ndarray
        Atomic types represented by integer numbers starting from 1, which
        are used internally, but currently needed to specify cutoff radii
        in define method.
        shape=(num_atoms,), dtype='intc'

    """

    def __init__(self, lavec, xcoord, numbers, verbosity=0):
        """

        Parameters
        ----------
        lavec : array_like
            Basis vectors. a, b, c are given as column vectors.
            shape=(3, 3), dtype='double'
        xcoord : array_like
            Fractional coordinates of atomic points.
            shape=(num_atoms, 3), dtype='double'
        numbers : array_like
            Atomic numbers.
            shape=(num_atoms,), dtype='intc'
        verbosity : int
            Level of the output frequency either 0 (no output) or
            1 (normal output). Default is 0.

        """

        self._id = None
        self._cell = None
        self.lavec = lavec
        self.xcoord = xcoord
        self.numbers = numbers
        self._verbosity = verbosity
        self._kind_indices = None
        self._iconst = 11
        self._maxorder = 1

        self._output_filename_prefix = None

        # Whether python parameters are needed to be copied to C++ instance
        # or not.
        self._need_transfer = True

    @property
    def lavec(self):
        """Getter of basis vectors

        Returns
        -------
        lavec : ndarray
        Copy of basis vectors. a, b, c are given as column vectors.
        shape=(3, 3), dtype='double', order='C'

        """
        return np.array(self._lavec, dtype='double', order='C')

    @lavec.setter
    def lavec(self, lavec):
        """Setter of basis vectors

        Parameters
        ----------
        lavec : array_like
        Basis vectors. a, b, c are given as column vectors.
        shape=(3, 3), dtype='double', order='C'

        """

        self._need_transfer = True
        self._lavec = np.array(lavec, dtype='double', order='C')

    @property
    def xcoord(self):
        """Getter of atomic point coordinates

        Returns
        -------
        xcoord : ndarray
        Atomic point coordinates.
        shape=(num_atom, 3), dtype='double', order='C'

        """

        return np.array(self._xcoord, dtype='double', order='C')

    @xcoord.setter
    def xcoord(self, xcoord):
        """Setter of atomic point coordinates

        Returns
        -------
        xcoord : ndarray
        Atomic point coordinates.
        shape=(num_atom, 3), dtype='double', order='C'

        """

        self._need_transfer = True
        self._xcoord = np.array(xcoord, dtype='double', order='C')

    @property
    def numbers(self):
        """Getter of atomic numbers

        Returns
        -------
        numbers : ndarray
        Atomic numbers.
        shape=(num_atom,), dtype='intc', order='C'

        """

        return np.array(self._numbers, dtype='intc', order='C')

    @numbers.setter
    def numbers(self, numbers):
        """Setter of atomic numbers

        Parameters
        ----------
        numbers : ndarray
        Atomic numbers.
        shape=(num_atom,), dtype='intc', order='C'

        """

        self._need_transfer = True
        self._numbers = np.array(numbers, dtype='intc', order='C')

    @property
    def kind_indices(self):
        return self._kind_indices

    @property
    def verbosity(self):
        return self._verbosity

    @verbosity.setter
    def verbosity(self, verbosity):
        """Set verbosity of output.

        Parameters
        ----------
        verbosity : int
            Choose the level of the output frequency from
            0 (no output) or 1 (normal output).

        """

        self._verbosity = verbosity
        self._need_transfer = True

    def set_verbosity(self, verbosity):
        self.verbosity = verbosity

    def __enter__(self):
        self.alm_new()
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.alm_delete()

    def alm_new(self):
        """Create ALM instance in C++.

        This is also called by context manager when entering the block.

        ex.::

           with ALM(lavec, xcoord, numbers) as alm:

        Note
        ----
        When an ALM instance is created by ``alm_new``, it must be deleted
        by ``alm_delete`` to avoid memory leak.

        """

        if self._id is None:
            self._id = alm.alm_new()
            if self._id < 0:
                raise RuntimeError("Too many ALM objects")
            self._transfer_parameters()
        else:
            raise("This ALM object is already initialized.")

    def alm_delete(self):
        """Delete ALM instance in C++.

        This is also called by context manager when exiting the block.

        ex.::

           with ALM(lavec, xcoord, kind) as alm:

        """

        if self._id is None:
            self._show_error_message()

        alm.alm_delete(self._id)
        self._id = None

    def suggest(self):
        """Compute displacement patterns to obtain force constants."""

        if self._id is None:
            self._show_error_message()

        self._transfer_parameters()

        alm.suggest(self._id)

    def optimize(self, solver='dense'):
        """Fit force constants to forces.

        Parameters
        ----------
        solver : str, default='dense'
            Solver choice for fitting either 'dense' or 'SimplicialLDLT'.

            - When solver='dense', the fitting is performed with the
              singular value decomposition implemented in LAPACK.
            - When solver='SimplicialLDLT', the fitting is performed with
              the sparse solver class SimplicialLDLT implemented in
              Eigen3 library.

        Returns
        -------
        info : int
            This tells condition how fitting went.
            0 if the fitting is successful, 1 otherwise.

        """

        if self._id is None:
            self._show_error_message()

        self._transfer_parameters()

        if solver not in ['dense', 'SimplicialLDLT']:
            msgs = ["The given solver option is not supported.",
                    "Available options are 'dense' and 'SimplicialLDLT'."]
            raise ValueError("\n".join(msgs))

        info = alm.optimize(self._id, solver)

        return info

    @property
    def output_filename_prefix(self):
        return self._output_filename_prefix

    @output_filename_prefix.setter
    def output_filename_prefix(self, prefix):
        if self._id is None:
            self._show_error_message()

        if type(prefix) is str:
            self._output_filename_prefix = prefix
            alm.set_output_filename_prefix(self._id, prefix)

    def set_output_filename_prefix(self, prefix):
        """Set output prefix of output filename"""
        self.output_filename_prefix = prefix

    @property
    def optimizer_control(self):
        if self._id is None:
            self._show_error_message()

        optctrl = alm.get_optimizer_control(self._id)
        optcontrol = dict(zip(optimizer_control_keys, optctrl))
        return optcontrol

    def set_optimizer_control(self, optcontrol):
        if self._id is None:
            self._show_error_message()

        keys = optimizer_control_keys
        optctrl = []
        optcontrol_l = {key.lower(): optcontrol[key] for key in optcontrol}

        for i, key in enumerate(optcontrol):
            if key.lower() not in keys:
                msg = "%s is not a valide key for optimizer control." % key
                raise KeyError(msg)

        for i, key in enumerate(keys):
            if key in optcontrol_l:
                optctrl.append(optcontrol_l[key])
            else:
                optctrl.append(None)

        alm.set_optimizer_control(self._id, optctrl)

    def set_training_data(self, u, f):
        """Set displacements and respective forces in supercell.

        Parameters
        ----------
        u : array_like
            Atomic displacement patterns in supercells in Cartesian.
            dtype='double'
            shape=(supercells, num_atoms, 3)
        f : array_like
            Forces in supercells.
            dtype='double'
            shape=(supercells, num_atoms, 3)

        """

        if self._id is None:
            self._show_error_message()

        if u.ndim != 3:
            msg = "Displacement array has to be three dimensions."
            raise RuntimeError(msg)
        if f.ndim != 3:
            msg = "Force array has to be three dimensions."
            raise RuntimeError(msg)

        alm.set_training_data(
            self._id,
            np.array(u, dtype='double', order='C'),
            np.array(f, dtype='double', order='C'))

    def set_displacement_and_force(self, u, f):
        warnings.warn("ALM.set_displacement_and_force is deprecated. "
                      "Use ALM.set_displacement_and_force is deprecated.",
                      DeprecationWarning)

        self.set_training_data(u, f)

    def define(self, maxorder, cutoff_radii=None, nbody=None,
               symmetrization_basis='Lattice'):
        """Define the Taylor expansion potential.

        Parameters
        ----------
        maxorder : int
            Maximum order of the Taylor expansion potential.

            - If ``maxorder = 1``, only harmonic (2nd-order) terms are
              considered.
            - If ``maxorder = 2``, both harmonic and cubic terms are
              considered.

        cutoff_radii : array_like, default = None
            Cutoff radii defined for each order.
            When a negative value is provided, the cutoff radius is not used.
            dtype='double'
            shape=(maxorder, num_elems, num_elems)

        nbody : array_like, default = None
            Option to neglect multi-body interactions.
            dtype='intc'
            shape=(maxorder,)

        symmetrization_basis : str, default='Lattice'
            Either 'Cartesian' or 'Lattice'. Symmetrization of force constants
            is done either in the matrix based on crystal coordinates
            ('Lattice') or Cartesian coordinates ('Cartesian').

        """

        if self._id is None:
            self._show_error_message()

        self._transfer_parameters()

        if nbody is None:
            nbody = []
            for i in range(maxorder):
                nbody.append(i + 2)

        else:
            if len(nbody) != maxorder:
                msg = "The size of nbody must be equal to maxorder."
                raise RuntimeError(msg)

        if cutoff_radii is None:
            _cutoff_radii = None
        else:
            _cutoff_radii = np.array(cutoff_radii, dtype='double', order='C')
            nelem = len(_cutoff_radii.ravel())
            if (nelem // maxorder) * maxorder != nelem:
                msg = "The array shape of cutoff_radii is wrong."
                raise RuntimeError(msg)
            nkd = int(round(np.sqrt(nelem // maxorder)))
            if nkd ** 2 - nelem // maxorder != 0:
                msg = "The array shape of cutoff_radii is wrong."
                raise RuntimeError(msg)
            _cutoff_radii = np.reshape(_cutoff_radii, (maxorder, nkd, nkd),
                                       order='C')

        self._maxorder = maxorder

        if symmetrization_basis.lower() in ['lattice', 'cartesian']:
            fc_basis = symmetrization_basis.capitalize()
        else:
            fc_basis = 'Lattice'

        alm.define(self._id,
                   maxorder,
                   np.array(nbody, dtype='intc'),
                   _cutoff_radii,
                   fc_basis)

        alm.generate_force_constant(self._id)

    def set_constraint(self, translation=True, rotation=False):
        """Set constraints for the translational and rotational invariances

        Parameters
        ----------
        translation : bool, optional (default = True)
            When set to ``True``, the translational invariance (aka acoustic sum rule)
            is imposed between force constants.

        rotation : bool, optional (default = False)
            When set to ``True``, the rotational invariance is imposed between
            force constants. This function is not implemented.

        """

        if rotation is True:
            raise("Rotational invariance is not supported in API.")

        if translation is True:
            iconst = 11
        else:
            iconst = 10

        self._iconst = iconst
        alm.set_constraint_type(self._id, self._iconst)

    def getmap_primitive_to_supercell(self):
        """Returns the mapping information from the primitive cell to the supercell.

        Returns
        -------
        map_p2s : array_like
            The mapping information of atoms from the primitive cell to the
            supercell.
            dtype='intc'
            shape = (num_trans, num_atoms_primitive)

        """

        if self._id is None:
            self._show_error_message()

        map_p2s = np.zeros(len(self._xcoord), dtype='intc')
        ntrans = alm.get_atom_mapping_by_pure_translations(self._id, map_p2s)
        return map_p2s.reshape((ntrans, -1))

    def get_displacement_patterns(self, fc_order):
        """Returns the displacement patterns to obtain force constants.

        Parameters
        ----------
        fc_order : int
            The order of force constants to get the displacement patterns.

            - If ``fc_order = 1``, returns patterns for harmonic force
              constants.
            - If ``fc_order = 2``, returns patterns for cubic force constants.
            - If ``fc_order = 3``, returns patterns for quartic force
              constants.
            - ...

        Returns
        -------
        all_disps : array_like, shape = (n_patterns,)
            The array of tuples (``atom_index``, ``direction``, ``basis``),
            where ``direction`` is the numpy.ndarray of size = (3,) representing
            the direction of the displacement, and ``basis`` is a string
            either "Cartesian" or "Fractional".

        """

        if self._id is None:
            self._show_error_message()

        if fc_order > self._maxorder:
            msg = ("The fc_order must not be larger than the maximum order "
                   "(maxorder).")
            raise ValueError(msg)

        numbers = self._get_number_of_displaced_atoms(fc_order)
        tot_num = np.sum(numbers)
        atom_indices = np.zeros(tot_num, dtype='intc')
        disp_patterns = np.zeros((tot_num, 3), dtype='double', order='C')
        nbasis = alm.get_displacement_patterns(self._id,
                                               atom_indices,
                                               disp_patterns,
                                               fc_order)
        basis = ["Cartesian", "Fractional"][nbasis]
        all_disps = []
        pos = 0
        for num in numbers:
            disp = []
            for i in range(num):
                disp.append((atom_indices[pos], disp_patterns[pos], basis))
                pos += 1
            all_disps.append(disp)
        return all_disps

    def get_fc(self, fc_order, mode="origin"):
        """Returns the force constant values

        Parameters
        ----------
        fc_order : int
            The order of force constants to get.

            - If ``fc_order = 1``, returns harmonic force constants.
            - If ``fc_order = 2``, returns cubic force constants.
            - If ``fc_order = 3``, returns quartic force constants.
            - ...

        mode : str, optional (default="origin")
            The choice of the force constant list to be returned.

            - If "origin", returns the reducible set of force constants,
              whose first element corresponds to an atom in the
              primitive cell at the origin.
            - If "irreducible" or "irred", returns the irreducible set of
              force constants.
            - If "all", returns the all non-zero elements of force constants
              in the supercell.

        Returns
        -------
        fc_values : array_like, dtype='double', shape=(num_fc,)
            Force constant values.

        elem_indices : array_like, dtype='int', shape=(num_fc, fc_order + 1)
            Array of flattened indices 3 * index_atom + index_xyz.

        Note
        ----
        This method does not return force constants elements that
        can be replicated by the permutation of indices.

        """

        if self._id is None:
            self._show_error_message()

        if fc_order > self._maxorder:
            msg = ("The fc_order must not be larger than the maximum order "
                   "(maxorder).")
            raise ValueError(msg)

        if mode == "origin":

            fc_length = self._get_number_of_fc_elements(fc_order)
            fc_values = np.zeros(fc_length, dtype='double')
            elem_indices = np.zeros((fc_length, fc_order + 1),
                                    dtype='intc', order='C')

            alm.get_fc_origin(self._id, fc_values, elem_indices)

            return fc_values, elem_indices

        elif mode == "irreducible" or mode == "irred":

            fc_length = self._get_number_of_irred_fc_elements(fc_order)
            fc_values = np.zeros(fc_length, dtype='double')
            elem_indices = np.zeros((fc_length, fc_order + 1),
                                    dtype='intc', order='C')

            alm.get_fc_irreducible(self._id, fc_values, elem_indices)

            return fc_values, elem_indices

        elif mode == "all":

            map_p2s = np.zeros(len(self._xcoord), dtype='intc')
            ntrans = alm.get_atom_mapping_by_pure_translations(self._id,
                                                               map_p2s)
            fc_length = self._get_number_of_fc_elements(fc_order) * ntrans
            fc_values = np.zeros(fc_length, dtype='double')
            elem_indices = np.zeros((fc_length, fc_order + 1),
                                    dtype='intc', order='C')

            alm.get_fc_all(self._id, fc_values, elem_indices)

            return fc_values, elem_indices

        else:
            raise ValueError("Invalid mode in get_fc.")

    def set_fc(self, fc_in):
        """Copy force constant obtained by an external optimizer to the ALM instance.

        Parameters
        ----------
        fc_in : array_like
            The irreducible set of force constants.
            dtype='double'
            shape=(num_fc,)

        Note
        ----
        When an external optimizer, such as numpy.linalg.lstsq, is used to fit
        force constants, the force constants need to be passed to the ALM instance
        by ``set_fc`` to use the ``get_fc`` method.

        """

        if self._id is None:
            self._show_error_message()

        maxorder = self._maxorder
        fc_length_irred = 0
        for i in range(maxorder):
            fc_length_irred += self._get_number_of_irred_fc_elements(i + 1)

        if fc_length_irred != len(fc_in):
            msg = "The size of the given force constant array is incorrect."
            raise RuntimeError(msg)

        alm.set_fc(self._id, np.array(fc_in, dtype='double', order='C'))

    def get_matrix_elements(self):
        """Returns the sensing matrix A and force vector b

        Returns
        -------
        amat : ndarray, dtype='double'
            shape=(3 * num_atoms * ndata_training, num_fc_irred), order='F'.
            The sensing matrix A calculated from the displacements.

        bvec : ndarray, dtype='double'
            shape=(3 * num_atoms * ndata_training,)
            The vector b calculated from the atomic forces.


        Note
        ----
        From the amat (``A``) and bvec (``b``), the force constant vector ``x``
        can be obtained by solving the least-square problem:
        x = argmin_{x} | Ax-b|^{2}.

        """

        if self._id is None:
            self._show_error_message()

        maxorder = self._maxorder
        nrows = self._get_nrows_amat()

        fc_length = 0
        for i in range(maxorder):
            fc_length += self._get_number_of_irred_fc_elements(i + 1)

        amat = np.zeros(nrows * fc_length, dtype='double', order='C')
        bvec = np.zeros(nrows, dtype='double')
        alm.get_matrix_elements(self._id, amat, bvec)

        return (np.reshape(amat, (nrows, fc_length), order='F'), bvec)

    def get_cv_l1_alpha(self):
        """Returns L1 alpha at minimum CV"""

        if self._id is None:
            self._show_error_message()

        return alm.get_cv_l1_alpha(self._id)

    def _transfer_parameters(self):
        if self._need_transfer:
            self._set_cell()
            self._set_verbosity()
            self._need_transfer = False

    def _set_cell(self):
        """Inject crystal structure in C++ instance"""

        if self._id is None:
            self._show_error_message()

        if self._lavec is None:
            msg = "Basis vectors are not set."
            raise RuntimeError(msg)

        if self._xcoord is None:
            msg = "Atomic point coordinates (positions) are not set."
            raise RuntimeError(msg)

        if self._numbers is None:
            msg = "Atomic numbers are not set."
            raise RuntimeError(msg)

        if len(self._xcoord) != len(self._numbers):
            msg = "Numbers of atomic points and atomic numbers don't agree."
            raise RuntimeError(msg)

        self._kind_indices = np.zeros_like(self._numbers)
        alm.set_cell(self._id, self._lavec, self._xcoord, self._numbers,
                     self._kind_indices)
        self._cell = (self._lavec, self._xcoord, self._numbers)

    def _set_verbosity(self):
        """Inject verbosity in C++ instance."""

        if self._id is None:
            self._show_error_message()

        alm.set_verbosity(self._id, self._verbosity)

    def _get_nrows_amat(self):
        """Private method to return the number of training data sets"""
        if self._id is None:
            self._show_error_message()
        nrows_amat = alm.get_nrows_amat(self._id)
        return nrows_amat

    def _get_id(self):
        """Private method to return the instance ID"""
        return self._id

    def _get_number_of_displacement_patterns(self, fc_order):
        """Private method to return the number of displacement patterns

        Parameters
        ----------
        fc_order : int
            The order of force constants.
            fc_order = 1 for harmonic, fc_order = 2 for cubic ...

        """

        return alm.get_number_of_displacement_patterns(self._id, fc_order)

    def _get_number_of_displaced_atoms(self, fc_order):
        """Private method to return the number of displaced atoms

        Parameters
        ----------
        fc_order : int
            The order of force constants.
            fc_order = 1 for harmonic, fc_order = 2 for cubic ...

        """

        num_disp_patterns = self._get_number_of_displacement_patterns(
            fc_order)
        numbers = np.zeros(num_disp_patterns, dtype='intc')
        alm.get_number_of_displaced_atoms(self._id, numbers, fc_order)
        return numbers

    def _get_number_of_fc_elements(self, fc_order):
        """Private method to get the number of force constants

        Parameters
        ----------
        fc_order : int
            The order of force constants.
            fc_order = 1 for harmonic, fc_order = 2 for cubic ...

        """

        return alm.get_number_of_fc_elements(self._id, fc_order)

    def _get_number_of_irred_fc_elements(self, fc_order):
        """Private method to get the number of irreducible set of force constants

        Parameters
        ----------
        fc_order : int
            The order of force constants.
            fc_order = 1 for harmonic, fc_order = 2 for cubic ...

        """
        return alm.get_number_of_irred_fc_elements(self._id, fc_order)

    def _show_error_message(self):
        """Private method to raise an error"""
        msg = "This ALM object has to be initialized by ALM::alm_new()"
        raise RuntimeError(msg)
