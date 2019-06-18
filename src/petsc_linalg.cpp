
#include "petsc_interface.hpp"

namespace ngs_petsc_interface
{

  template<class TM> INLINE typename ngs::mat_traits<TM>::TSCAL* get_ptr(TM & val) { return &val(0,0); }
  template<> INLINE ngs::mat_traits<double>::TSCAL* get_ptr<double>(double & val) { return &val; }

  template<class TM>
  void SetPETScMatSeq (PETScMat petsc_mat, shared_ptr<ngs::SparseMatrixTM<TM>> spmat, shared_ptr<ngs::BitArray> rss, shared_ptr<ngs::BitArray> css)
  {
    PetscInt bs; MatGetBlockSize(petsc_mat, &bs);
    if (bs != ngs::mat_traits<TM>::WIDTH) {
      throw Exception(string("Block-Size of petsc-mat (") + to_string(bs) + string(") != block-size of ngs-mat(")
		      + to_string(ngs::mat_traits<TM>::WIDTH) + string(")"));
    }
	
    // row map (map for a row)
    PetscInt bw = ngs::mat_traits<TM>::WIDTH;
    int nbrow = 0;
    Array<int> row_compress(spmat->Width());
    for (auto k : Range(spmat->Width()))
      { row_compress[k] = (!rss || rss->Test(k)) ? nbrow++ : -1; }
    int ncols = nbrow * bw;
    
    // col map (map for a col)
    PetscInt bh = ngs::mat_traits<TM>::HEIGHT;
    int nbcol = 0;
    Array<int> col_compress(spmat->Height());
    for (auto k : Range(spmat->Height()))
      { col_compress[k] = (!css || css->Test(k)) ? nbcol++ : -1; }
    int nrows = nbcol * bh;
    
    // vals
    size_t len_vals = 0;
    for (auto k : Range(spmat->Height())) {
      PetscInt ck = col_compress[k];
      if (ck != -1) {
	auto ris = spmat->GetRowIndices(k);
	auto rvs = spmat->GetRowValues(k);
	for (auto j : Range(ris.Size())) {
	  PetscInt cj = row_compress[ris[j]];
	  if (cj != -1) {
	    PetscScalar* data = get_ptr(rvs[j]);
	    MatSetValuesBlocked(petsc_mat, 1, &ck, 1, &cj, data, INSERT_VALUES);
	  }
	}
      }
    }
    MatAssemblyBegin(petsc_mat, MAT_FINAL_ASSEMBLY);
    MatAssemblyEnd(petsc_mat, MAT_FINAL_ASSEMBLY);
  }

  template<class TM>
  PETScMat CreatePETScMatSeqBAIJ (shared_ptr<ngs::SparseMatrixTM<TM>> spmat, shared_ptr<ngs::BitArray> rss, shared_ptr<ngs::BitArray> css)
  {

    static_assert(ngs::mat_traits<TM>::WIDTH == ngs::mat_traits<TM>::HEIGHT, "PETSc can only handle square block entries!");

    // row map (map for a row)
    PetscInt bw = ngs::mat_traits<TM>::WIDTH;
    int nbrow = 0;
    Array<int> row_compress(spmat->Width());
    for (auto k : Range(spmat->Width()))
      { row_compress[k] = (!rss || rss->Test(k)) ? nbrow++ : -1; }
    int ncols = nbrow * bw;
    
    // col map (map for a col)
    PetscInt bh = ngs::mat_traits<TM>::HEIGHT;
    int nbcol = 0;
    Array<int> col_compress(spmat->Height());
    for (auto k : Range(spmat->Height()))
      { col_compress[k] = (!css || css->Test(k)) ? nbcol++ : -1; }
    int nrows = nbcol * bh;

    // allocate mat
    Array<PetscInt> nzepr(nbcol); nzepr = 0;
    nbcol = 0;
    for (auto k : Range(spmat->Height())) {
      if (!css | css->Test(k)) {
	auto & c = nzepr[nbcol++];
	for (auto j : spmat->GetRowIndices(k))
	  if (!rss || rss->Test(j))
	    { c++; }
      }
    }
    PETScMat petsc_mat; MatCreateSeqBAIJ(PETSC_COMM_SELF, bh, nrows, ncols, -1, &nzepr[0], &petsc_mat); 

    // cols
    int n_b_entries = 0;
    for (auto k : Range(nzepr.Size()))
      { n_b_entries += nzepr[k]; }
    Array<PetscInt> cols(n_b_entries);
    n_b_entries = 0;
    for (auto k : Range(spmat->Height())) {
      if (!css || css->Test(k)) {
	for (auto j : spmat->GetRowIndices(k))
	  if (!rss || rss->Test(j))
	    { cols[n_b_entries++] = row_compress[j]; }
      }
    }
    MatSeqBAIJSetColumnIndices(petsc_mat, &cols[0]);

    // vals
    size_t len_vals = 0;
    for (auto k : Range(spmat->Height())) {
      PetscInt ck = col_compress[k];
      if (ck != -1) {
	auto ris = spmat->GetRowIndices(k);
	auto rvs = spmat->GetRowValues(k);
	for (auto j : Range(ris.Size())) {
	  PetscInt cj = row_compress[ris[j]];
	  if (cj != -1) {
	    PetscScalar* data = get_ptr(rvs[j]);
	    MatSetValuesBlocked(petsc_mat, 1, &ck, 1, &cj, data, INSERT_VALUES);
	  }
	}
      }
    }
    MatAssemblyBegin(petsc_mat, MAT_FINAL_ASSEMBLY);
    MatAssemblyEnd(petsc_mat, MAT_FINAL_ASSEMBLY);

    // cout << "SPMAT: " << endl << *spmat << endl;
    // cout << "PETSC BLOCK-MAT: " << endl;
    // MatView(petsc_mat, PETSC_VIEWER_STDOUT_SELF);
    return petsc_mat;
  } // CreatePETScMatSeqBAIJ


  PETScMat CreatePETScMatSeq (shared_ptr<ngs::BaseMatrix> mat, shared_ptr<ngs::BitArray> rss, shared_ptr<ngs::BitArray> css)
  {
    if (auto spm = dynamic_pointer_cast<ngs::SparseMatrixTM<double>>(mat))
      { return CreatePETScMatSeqBAIJ(spm, rss, css); }
#if MAX_SYS_DIM>=2
    if (auto spm = dynamic_pointer_cast<ngs::SparseMatrixTM<ngs::Mat<2>>>(mat))
      { return CreatePETScMatSeqBAIJ(spm, rss, css); }
#endif
#if MAX_SYS_DIM>=3
    if (auto spm = dynamic_pointer_cast<ngs::SparseMatrixTM<ngs::Mat<3>>>(mat))
      { return CreatePETScMatSeqBAIJ(spm, rss, css); }
#endif
#if MAX_SYS_DIM>=4
    if (auto spm = dynamic_pointer_cast<ngs::SparseMatrixTM<ngs::Mat<4>>>(mat))
      { return CreatePETScMatSeqBAIJ(spm, rss, css); }
#endif
#if MAX_SYS_DIM>=5
    if (auto spm = dynamic_pointer_cast<ngs::SparseMatrixTM<ngs::Mat<5>>>(mat))
      { return CreatePETScMatSeqBAIJ(spm, rss, css); }
#endif
#if MAX_SYS_DIM>=6
    if (auto spm = dynamic_pointer_cast<ngs::SparseMatrixTM<ngs::Mat<6>>>(mat))
      { return CreatePETScMatSeqBAIJ(spm, rss, css); }
#endif
#if MAX_SYS_DIM>=7
    if (auto spm = dynamic_pointer_cast<ngs::SparseMatrixTM<ngs::Mat<6>>>(mat))
      { return CreatePETScMatSeqBAIJ(spm, rss, css); }
#endif
#if MAX_SYS_DIM>=8
    if (auto spm = dynamic_pointer_cast<ngs::SparseMatrixTM<ngs::Mat<6>>>(mat))
      { return CreatePETScMatSeqBAIJ(spm, rss, css); }
#endif
    throw Exception("Cannot make PETSc-Mat from this NGSolve-Mat!");
    return PETScMat(NULL);
  } // CreatePETScMatSeq


  void PETScBaseMatrix :: SetNullSpace (MatNullSpace null_space)
  {
    MatSetNullSpace(GetPETScMat(), null_space);
  }


  void PETScBaseMatrix :: SetNearNullSpace (MatNullSpace null_space)
  {
    MatSetNearNullSpace(GetPETScMat(), null_space);
  }


  PETScMat CreatePETScMatIS (PETScMat petsc_mat_loc,
			     shared_ptr<ngs::ParallelDofs> row_pardofs,
			     shared_ptr<ngs::ParallelDofs> col_pardofs,
			     shared_ptr<BitArray> rss,
			     shared_ptr<BitArray> css)
  {

    bool same_spaces = ( (row_pardofs == col_pardofs) && (rss == css) );
    
    auto create_map = [&](auto& glob_nd, auto & pardofs, auto & subset) -> ISLocalToGlobalMapping {
      PetscInt bs = pardofs->GetEntrySize();
      // NGSolve global enumeration (all not in subset get -1)
      Array<int> globnums;
      pardofs->EnumerateGlobally(subset, globnums, glob_nd);
      // Only subset numbers
      Array<PetscInt> compress_globnums(subset ? subset->NumSet() : pardofs->GetNDofLocal());
      PetscInt loc_nfd = 0; // map needs number of local FREE dofs
      for (auto k : Range(pardofs->GetNDofLocal()))
	if (globnums[k]!=-1)
	  compress_globnums[loc_nfd++] = globnums[k];
      int loc_nfr = loc_nfd * bs;
      ISLocalToGlobalMapping map;
      ISLocalToGlobalMappingCreate(pardofs->GetCommunicator(), bs, loc_nfd, &compress_globnums[0], PETSC_COPY_VALUES, &map);
      return map;
    };

    int glob_nd_col = 0, glob_nd_row = 0;
    ISLocalToGlobalMapping petsc_map_row = create_map(glob_nd_row, row_pardofs, rss);
    ISLocalToGlobalMapping petsc_map_col = same_spaces ? NULL : create_map(glob_nd_col, col_pardofs, css);
    if (same_spaces) glob_nd_col = glob_nd_row;
    
    // count subset + master dofs
    auto count_ssm = [&](auto & pardofs, auto & subset) -> PetscInt {
      PetscInt x = 0;
      for (auto k : Range(pardofs->GetNDofLocal()))
	if ((!subset || subset->Test(k)) && pardofs->IsMasterDof(k))
	  { x++; }
      return x;
    };

    auto bs = row_pardofs->GetEntrySize();

    PetscInt n_ssm_row = count_ssm (row_pardofs, rss);
    PetscInt n_rows_owned = n_ssm_row * bs;

    PetscInt n_ssm_col = same_spaces ? n_ssm_row : count_ssm (row_pardofs, rss);
    PetscInt n_cols_owned = n_ssm_col * bs;
    
    PetscInt glob_nrows = glob_nd_col * bs;
    PetscInt glob_ncols = glob_nd_row * bs;

    // cout << "glob br/bc " << glob_nd_col << " x " << glob_nd_row << endl;
    // cout << "glob r/c   " << glob_nrows << " x " << glob_ncols << endl;
    
    PETScMat petsc_mat; MatCreateIS(row_pardofs->GetCommunicator(), bs, n_rows_owned, n_cols_owned, glob_nrows, glob_ncols, petsc_map_col, petsc_map_row, &petsc_mat);

    MatISSetLocalMat(petsc_mat, petsc_mat_loc);
    MatAssemblyBegin(petsc_mat, MAT_FINAL_ASSEMBLY);
    MatAssemblyEnd(petsc_mat, MAT_FINAL_ASSEMBLY);

    return petsc_mat;
  } // CreatePETScMatIS


  NGs2PETScVecMap :: NGs2PETScVecMap (size_t _ndof, int _bs, shared_ptr<ngs::ParallelDofs> _pardofs, shared_ptr<ngs::BitArray> _subset)
    : ndof(_ndof), bs(_bs), pardofs(_pardofs), subset(_subset)
  {
    if ( (!pardofs) && (!subset) )
      { nrows_loc = bs * ndof; }
    else {
      nrows_loc = 0;
      for (auto k : Range(ndof))
	if ( (!pardofs || pardofs->IsMasterDof(k)) && (!subset || subset->Test(k)))
	  nrows_loc += bs;
    }
    nrows_glob = (pardofs == nullptr) ? nrows_loc : pardofs->GetCommunicator().AllReduce(nrows_loc, MPI_SUM);

    cout << "ba len: " << (subset ? subset->Size() : ndof) << endl;
    cout << "ba set: " << (subset ? subset->NumSet() : ndof) << endl;
    cout << "vec map, " << ndof << " " << bs << " " << nrows_loc << " " << nrows_glob << endl;

  }


  void NGs2PETScVecMap :: NGs2PETSc (ngs::BaseVector& ngs_vec, PETScVec petsc_vec)
  {
    ngs_vec.Cumulate();
    PetscScalar * pvs; VecGetArray(petsc_vec, &pvs);
    size_t cnt = 0;
    auto fv = ngs_vec.FVDouble();
    for (auto k : Range(ndof))
      if ( (!pardofs || pardofs->IsMasterDof(k)) && (!subset || subset->Test(k)))
	for (auto l : Range(bs))
	  { pvs[cnt++] = fv(bs*k+l); }
    VecRestoreArray(petsc_vec, &pvs);
  } // NGs2PETSc


  void NGs2PETScVecMap :: PETSc2NGs (ngs::BaseVector& ngs_vec, PETScVec petsc_vec)
  {
    ngs_vec.Distribute();
    const PetscScalar * pvs; VecGetArrayRead(petsc_vec, &pvs);
    size_t cnt = 0;
    auto fv = ngs_vec.FVDouble();
    for (auto k : Range(ndof))
      if ( (!pardofs || pardofs->IsMasterDof(k)) && (!subset || subset->Test(k)))
	for (auto l : Range(bs))
	  { fv(bs*k+l) = pvs[cnt++]; }
      else
	for (auto l : Range(bs))
	  { fv(bs*k+l) = 0; }
    VecRestoreArrayRead(petsc_vec, &pvs);
  } // PETSc2NGs


  shared_ptr<ngs::BaseVector> NGs2PETScVecMap :: CreateNGsVector () const
  {
    if (pardofs)
      { return make_shared<ngs::S_ParallelBaseVectorPtr<double>> (pardofs->GetNDofLocal(), pardofs->GetEntrySize(), pardofs, ngs::DISTRIBUTED); }
    else
      { return make_shared<ngs::S_BaseVectorPtr<double>> (ndof, bs); }
  } // CreateNGsVector

  PETScVec NGs2PETScVecMap :: CreatePETScVector () const
  {
    PETScVec v;
    if (pardofs == nullptr)
      { VecCreateSeq(PETSC_COMM_SELF, nrows_loc, &v); }
    else
      { VecCreateMPI(pardofs->GetCommunicator(), nrows_loc, nrows_glob, &v); }
    return v;
  } // CreatePETScVector


  PETScMatrix :: PETScMatrix (shared_ptr<ngs::BaseMatrix> _ngs_mat, shared_ptr<ngs::BitArray> _row_subset,
			      shared_ptr<ngs::BitArray> _col_subset, PETScMatType _petsc_mat_type)
    : PETScBaseMatrix(_ngs_mat, _row_subset, _col_subset)
  {
    auto parmat = dynamic_pointer_cast<ngs::ParallelMatrix>(ngs_mat);

    bool parallel = parmat != nullptr;

    shared_ptr<ngs::ParallelDofs> row_pardofs, col_pardofs;
    if (parallel) {
      row_pardofs = parmat->GetRowParallelDofs();
      col_pardofs = parmat->GetColParallelDofs();
    }
    else
      { row_pardofs = col_pardofs = nullptr; }

    shared_ptr<ngs::BaseSparseMatrix> spmat = dynamic_pointer_cast<ngs::BaseSparseMatrix>( parallel ? parmat->GetMatrix() : ngs_mat);
    if (!spmat) { throw Exception("Can only convert Sparse Matrices to PETSc."); }

    // local PETSc matrix
    PETScMat petsc_mat_loc = CreatePETScMatSeq(spmat, row_subset, col_subset);

    // parallel PETSc matrix
    petsc_mat = parallel ? CreatePETScMatIS (petsc_mat_loc, row_pardofs, col_pardofs, row_subset, col_subset) : petsc_mat_loc;

    int bs; MatGetBlockSize(petsc_mat, &bs);
    
    MatType pmt; MatGetType(petsc_mat, &pmt);
    if (pmt != _petsc_mat_type)
      {
	// MatSetBlockSize(petsc_mat, row_pardofs->GetEntrySize());
	MatConvert(petsc_mat, _petsc_mat_type, MAT_INPLACE_MATRIX, &petsc_mat);
      }

    // Vector conversions
    if (!row_map) {
      row_map = parallel ? make_shared<NGs2PETScVecMap>(row_pardofs->GetNDofLocal(), bs, row_pardofs, row_subset)
	: make_shared<NGs2PETScVecMap>(spmat->Width(), bs, nullptr, row_subset);
    }
    if (!col_map) {
      col_map = parallel ? make_shared<NGs2PETScVecMap>(col_pardofs->GetNDofLocal(), bs, col_pardofs, col_subset)
	: make_shared<NGs2PETScVecMap>(spmat->Height(), bs, nullptr, row_subset);
    }

  } // PETScMatrix


  void PETScMatrix :: UpdateValues ()
  {
    auto parmat = dynamic_pointer_cast<ngs::ParallelMatrix>(ngs_mat);
    shared_ptr<BaseMatrix> mat = (parmat == nullptr) ? ngs_mat : parmat->GetMatrix();
    if (auto spmat = dynamic_pointer_cast<ngs::SparseMatrixTM<double>>(mat))
      { SetPETScMatSeq (petsc_mat, spmat, GetRowMap()->GetSubSet(), GetColMap()->GetSubSet()); }
    else if (auto spmat = dynamic_pointer_cast<ngs::SparseMatrixTM<Mat<2,2,double>>>(mat))
      { SetPETScMatSeq (petsc_mat, spmat, GetRowMap()->GetSubSet(), GetColMap()->GetSubSet()); }
    else if (auto spmat = dynamic_pointer_cast<ngs::SparseMatrixTM<Mat<3,3,double>>>(mat))
      { SetPETScMatSeq (petsc_mat, spmat, GetRowMap()->GetSubSet(), GetColMap()->GetSubSet()); }
    else if (auto spmat = dynamic_pointer_cast<ngs::SparseMatrixTM<Mat<4,4,double>>>(mat))
      { SetPETScMatSeq (petsc_mat, spmat, GetRowMap()->GetSubSet(), GetColMap()->GetSubSet()); }
    else if (auto spmat = dynamic_pointer_cast<ngs::SparseMatrixTM<Mat<5,5,double>>>(mat))
      { SetPETScMatSeq (petsc_mat, spmat, GetRowMap()->GetSubSet(), GetColMap()->GetSubSet()); }
    else if (auto spmat = dynamic_pointer_cast<ngs::SparseMatrixTM<Mat<6,6,double>>>(mat))
      { SetPETScMatSeq (petsc_mat, spmat, GetRowMap()->GetSubSet(), GetColMap()->GetSubSet()); }
    else if (auto spmat = dynamic_pointer_cast<ngs::SparseMatrixTM<Mat<7,7,double>>>(mat))
      { SetPETScMatSeq (petsc_mat, spmat, GetRowMap()->GetSubSet(), GetColMap()->GetSubSet()); }
    else if (auto spmat = dynamic_pointer_cast<ngs::SparseMatrixTM<Mat<8,8,double>>>(mat))
      { SetPETScMatSeq (petsc_mat, spmat, GetRowMap()->GetSubSet(), GetColMap()->GetSubSet()); }
    else
      { throw Exception("Can not update values for this kind of mat!!");}
  }


  FlatPETScMatrix :: FlatPETScMatrix (shared_ptr<ngs::BaseMatrix> _ngs_mat, shared_ptr<ngs::BitArray> _row_subset,
				      shared_ptr<ngs::BitArray> _col_subset, shared_ptr<NGs2PETScVecMap> _row_map,
				      shared_ptr<NGs2PETScVecMap> _col_map)
    : PETScBaseMatrix(_ngs_mat, _row_subset, _col_subset, _row_map, _col_map)
  {

    shared_ptr<ngs::ParallelDofs> row_pardofs, col_pardofs;
    if (auto parmat = dynamic_pointer_cast<ngs::ParallelMatrix>(ngs_mat)) {
      row_pardofs = parmat->GetRowParallelDofs();
      col_pardofs = parmat->GetColParallelDofs();
    }
    else if (auto pc = dynamic_pointer_cast<ngs::Preconditioner>(ngs_mat))
      { row_pardofs = col_pardofs = pc->GetAMatrix().GetParallelDofs(); }
    else // can also be a preconditioner, a ProductMatrix, etc..
      { row_pardofs = col_pardofs = GetNGsMat()->GetParallelDofs(); }

    bool parallel = row_pardofs != nullptr;

    MPI_Comm comm;
    if (parallel)
      { comm = row_pardofs->GetCommunicator(); }
    else
      { comm = PETSC_COMM_SELF; }

    // working vectors
    row_hvec = ngs_mat->CreateRowVector();
    col_hvec = ngs_mat->CreateColVector();

    int bs = (ngs_mat->Width() > 0) ? // rank 0 has size 0 mat
      row_hvec->FVDouble().Size() / ngs_mat->Width()
      : row_pardofs->GetEntrySize(); // this feels a bit hacky...

    cout << "fvs: " << row_hvec->FVDouble().Size() << endl;
    cout << "type vec " << typeid(*row_hvec).name() << endl;
    cout << "type amt; " << typeid(*ngs_mat).name() << endl;
    cout << "width: " << ngs_mat->Width() << endl;
    
    // Vector conversions
    cout << "RM " << endl;
    row_map = make_shared<NGs2PETScVecMap>(_ngs_mat->Width(), bs, row_pardofs, row_subset);
    cout << "CM " << endl;
    col_map = ( (row_pardofs == col_pardofs) && (_row_subset == _col_subset) ) ? row_map :
      make_shared<NGs2PETScVecMap>(_ngs_mat->Height(), bs, col_pardofs, col_subset);

    // working vectors
    // row_hvec = row_map->CreateNGsVector();
    // col_hvec = col_map->CreateNGsVector();

    // Create a Shell matrix, where we have to set function pointers for operations
    // ( the "this" - pointer can be recovered with MatShellGetConext )
    cout << "MATSHELL " << endl;
    cout << " " << row_map->GetNRowsLocal() << " " << col_map->GetNRowsLocal() << " " << row_map->GetNRowsGlobal() << " " << col_map->GetNRowsGlobal() << endl;
    MatCreateShell (comm, row_map->GetNRowsLocal(), col_map->GetNRowsLocal(), row_map->GetNRowsGlobal(), col_map->GetNRowsGlobal(), (void*) this, &petsc_mat);

    /** Set function pointers **/
    cout << "MATSHELL OPS" << endl;
    
    // MatMult: y = A * x
    MatShellSetOperation(petsc_mat, MATOP_MULT, (void(*)(void)) this->MatMult);
    
    cout << "MS DONE" << endl;

  } // FlatPETScMatrix


  PetscErrorCode FlatPETScMatrix :: MatMult (PETScMat A, PETScVec x, PETScVec y)
  {
    // y = A * x

    void* ptr; MatShellGetContext(A, &ptr);
    auto& FPM = *( (FlatPETScMatrix*) ptr);

    FPM.GetRowMap()->PETSc2NGs (*FPM.row_hvec, x);

    FPM.ngs_mat->Mult(*FPM.row_hvec, *FPM.col_hvec);

    FPM.GetColMap()->NGs2PETSc(*FPM.col_hvec, y);
    
    return PetscErrorCode(0);
  } // FlatPETScMatrix::MatMult


  MatNullSpace NullSpaceCreate (FlatArray<shared_ptr<ngs::BaseVector>> vecs, shared_ptr<NGs2PETScVecMap> map,
				bool is_orthonormal, bool const_kernel)
  {
    Array<PETScVec> petsc_vecs(vecs.Size());
    for (auto k : Range(vecs.Size())) {
      petsc_vecs[k] = map->CreatePETScVector();
      map->NGs2PETSc(*vecs[k], petsc_vecs[k]);
    }
    Array<double> dots(vecs.Size());
    if (!is_orthonormal) {
      VecNormalize(petsc_vecs[0],NULL);
      for (int i = 1; i < vecs.Size(); i++) {
	VecMDot(petsc_vecs[i],i,&petsc_vecs[0],&dots[0]);
	for (int j = 0; j < i; j++) dots[j] *= -1.;
	VecMAXPY(petsc_vecs[i],i,&dots[0],&petsc_vecs[0]);
	VecNormalize(petsc_vecs[i],NULL);
      }
    }
    MPI_Comm comm;
    if (auto pds = map->GetParallelDofs())
      { comm = pds->GetCommunicator(); }
    else
      { comm = PETSC_COMM_SELF; }
    MatNullSpace ns; MatNullSpaceCreate(comm, const_kernel ? PETSC_TRUE : PETSC_FALSE, vecs.Size(), &petsc_vecs[0], &ns);
    for (auto v : petsc_vecs) // destroy vecs (reduces reference count by 1)
      { VecDestroy(&v); }
    return ns;
  } // NullSpaceCreate


  void ExportLinAlg (py::module &m)
  {

    py::class_<PETScBaseMatrix, shared_ptr<PETScBaseMatrix>, ngs::BaseMatrix>
      (m, "PETScBaseMatrix", "Can be used as an NGSolve- or as a PETSc- Matrix")
      .def("SetNullSpace", [](shared_ptr<PETScBaseMatrix> & mat, py::list py_kvecs) {
	  Array<shared_ptr<ngs::BaseVector>> kvecs = makeCArraySharedPtr<shared_ptr<ngs::BaseVector>>(py_kvecs);
	  mat->SetNullSpace(NullSpaceCreate(kvecs, mat->GetRowMap()));
	}, py::arg("kvecs"))
      .def("SetNearNullSpace", [](shared_ptr<PETScBaseMatrix> & mat, py::list py_kvecs) {
	  Array<shared_ptr<ngs::BaseVector>> kvecs = makeCArraySharedPtr<shared_ptr<ngs::BaseVector>>(py_kvecs);
	  mat->SetNearNullSpace(NullSpaceCreate(kvecs, mat->GetRowMap()));
	}, py::arg("kvecs"));
    

    py::class_<PETScMatrix, shared_ptr<PETScMatrix>, PETScBaseMatrix>
      (m, "PETScMatrix", "PETSc matrix, converted from an NGSolve-matrix")
      .def(py::init<>
	   ([] (shared_ptr<ngs::BaseMatrix> mat, shared_ptr<ngs::BitArray> freedofs)
	    {
	      return make_shared<PETScMatrix> (mat, freedofs, freedofs, MATMPIAIJ);
	    }), py::arg("ngs_mat"), py::arg("freedofs") = nullptr);
		

    py::class_<FlatPETScMatrix, shared_ptr<FlatPETScMatrix>, PETScBaseMatrix>
      (m, "FlatPETScMatrix", "A wrapper around an NGSolve-matrix")
      .def(py::init<>
	   ([] (shared_ptr<ngs::BaseMatrix> mat, shared_ptr<ngs::BitArray> freedofs)
	    {
	      return make_shared<FlatPETScMatrix> (mat, freedofs, freedofs);
	    }), py::arg("ngs_mat"), py::arg("freedofs") = nullptr);

  }


} // namespace ngs_petsc_interface
