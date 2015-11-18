#include "lino_kkt.h"
/* #include "mex.h" */

/********** INIT **********/

/* Setup, allocate memory */
pfc* neSetup(idxint l, idxint ncones, idxint* q, spmat* G, spmat* A, pfloat delta){
	idxint i, conestart, S_nnz;
	/* Get pfc data structure */
	pfc* mypfc = (pfc*)MALLOC(sizeof(pfc));
	/* G */
	mypfc->G = G;
	/* A */
	mypfc->A = A;
	S_nnz = count_mem_diag(G);
	mypfc->S = newSparseMatrix(G->n,G->n,S_nnz);
	mypfc->Spattern = newSparseMatrix(G->n,G->n,S_nnz);
	for(i = 0; i <= G->n; i++){
		mypfc->S->jc[i] = 0;
		mypfc->Spattern->jc[i] = 0;
	}
	for(i = 0; i < S_nnz; i++){
		mypfc->S->ir[i] = 0;
		mypfc->S->pr[i] = 0;
		mypfc->Spattern->ir[i] = 0;
		mypfc->Spattern->pr[i] = 0;
	}

	/* Allocate memory for pointers to matrices and vectors */
	mypfc->GtG = (spmat**)MALLOC((l+ncones)*sizeof(spmat*));
	if(ncones){
		mypfc->G_br = (spmat**)MALLOC(ncones*sizeof(spmat*));
		mypfc->gtw = (pfloat**)MALLOC(ncones*sizeof(pfloat*));
		mypfc->gte = (pfloat**)MALLOC(ncones*sizeof(pfloat*));
		mypfc->wnew = (pfloat**)MALLOC(ncones*sizeof(pfloat*));
	}

	/* Allocate memory for blockrows&blockrow squares and compute them. Allocate memory for wnew, G'_i*w_i and G'_i*e0. */
	conestart = l; /* l = number of lp-cones */
	
	spmat* G_temp;
	for(i = 0; i < l; i++){
		G_temp = blockrow(G,i,i);
		mypfc->GtG[i] = sparseMtM(G_temp);
		freeSparseMatrix(G_temp);
	}

	for(i = 0; i < ncones; i++){	
		mypfc->G_br[i] = blockrow(G,conestart,conestart+q[i]-1);		
		mypfc->GtG[l+i] = sparseMtM(mypfc->G_br[i]);
		mypfc->gtw[i] = (pfloat*)MALLOC(G->n*sizeof(pfloat));
		mypfc->gte[i] = (pfloat*)MALLOC(G->n*sizeof(pfloat));
		mypfc->wnew[i] = (pfloat*)MALLOC(q[i]*sizeof(pfloat));
		conestart += q[i];
	}
	
	mypfc->ncones = ncones;
	mypfc->delta = delta;

	/* RHS */
	mypfc->xpGtWinv2z = (pfloat*)MALLOC(G->n*sizeof(pfloat));
	/*
	mypfc->bx = bx;
	mypfc->by = by;
	mypfc->bz = bz;
	*/
	mypfc->bxbybzsize = A->n+A->m+G->m;
	mypfc->bxbybz = (pfloat*)MALLOC(mypfc->bxbybzsize*sizeof(pfloat));
	
	/* Solution */
	mypfc->dx = (pfloat*)MALLOC(A->n*sizeof(pfloat));
	mypfc->dy = (pfloat*)MALLOC(A->m*sizeof(pfloat));
	mypfc->dz = (pfloat*)MALLOC(G->m*sizeof(pfloat));
	mypfc->workz = (pfloat*)MALLOC(G->m*sizeof(pfloat));
	
	/* Errors (for iterative refinement)*/
	mypfc->ex = (pfloat*)MALLOC(A->n*sizeof(pfloat));
	mypfc->ey = (pfloat*)MALLOC(A->m*sizeof(pfloat));
	mypfc->ez = (pfloat*)MALLOC((G->m)*sizeof(pfloat));
	
	/* Solution iterative refinement */
	mypfc->ddx = (pfloat*)MALLOC(A->n*sizeof(pfloat));
	mypfc->ddy = (pfloat*)MALLOC(A->m*sizeof(pfloat));
	mypfc->ddz = (pfloat*)MALLOC(G->m*sizeof(pfloat));

	
	/* Cholmod stuff */
	cholmod_l_start(&(mypfc->c));
	
	mypfc->Scm = cholmod_l_allocate_sparse(mypfc->S->m,mypfc->S->n,mypfc->S->nnz,1,1,1,CHOLMOD_REAL,&(mypfc->c));
	mypfc->Spatterncm = cholmod_l_allocate_sparse(mypfc->Spattern->m,mypfc->Spattern->n,mypfc->S->nnz,1,1,1,CHOLMOD_REAL,&(mypfc->c));
	mypfc->Acm = cholmod_l_allocate_sparse(mypfc->A->m,mypfc->A->n,mypfc->A->nnz,1,1,0,CHOLMOD_REAL,&(mypfc->c));
	for(i = 0; i <= mypfc->A->n; i++){
		((idxint*)mypfc->Acm->p)[i] = mypfc->A->jc[i];
	}
	for(i = 0; i < mypfc->A->nnz; i++){
		((idxint*)mypfc->Acm->i)[i] = mypfc->A->ir[i];
		((pfloat*)mypfc->Acm->x)[i] = mypfc->A->pr[i];
	}
	mypfc->Atcm = cholmod_l_transpose(mypfc->Acm,1,&(mypfc->c));
	mypfc->Gcm = cholmod_l_allocate_sparse(mypfc->G->m,mypfc->G->n,mypfc->G->nnz,1,1,0,CHOLMOD_REAL,&(mypfc->c));
	for(i = 0; i <= mypfc->G->n; i++){
		((idxint*)mypfc->Gcm->p)[i] = mypfc->G->jc[i];
	}
	for(i = 0; i < mypfc->G->nnz; i++){
		((idxint*)mypfc->Gcm->i)[i] = mypfc->G->ir[i];
		((pfloat*)mypfc->Gcm->x)[i] = mypfc->G->pr[i];
	}
	mypfc->RegS = cholmod_l_speye(mypfc->G->n,mypfc->G->n,CHOLMOD_REAL,&(mypfc->c));
	mypfc->RegM = cholmod_l_speye(mypfc->A->m,mypfc->A->m,CHOLMOD_REAL,&(mypfc->c));
	mypfc->xpGtWinv2zcm = cholmod_l_allocate_dense(mypfc->A->n,1,mypfc->A->n,CHOLMOD_REAL,&(mypfc->c));	
	mypfc->RHS = cholmod_l_allocate_dense(mypfc->A->m,1,mypfc->A->m,CHOLMOD_REAL,&(mypfc->c));	
	mypfc->bzcm = cholmod_l_allocate_dense(mypfc->G->m,1,mypfc->G->m,CHOLMOD_REAL,&(mypfc->c));
	mypfc->up_d = cholmod_l_allocate_dense(mypfc->S->m,1,mypfc->S->m,CHOLMOD_REAL,&(mypfc->c));
	mypfc->down_d = cholmod_l_allocate_dense(mypfc->S->m,1,mypfc->S->m,CHOLMOD_REAL,&(mypfc->c));
	
	/* Cholmod settings */
	mypfc->c.final_ll = 0;
	mypfc->c.final_pack = 0;
	mypfc->c.supernodal = CHOLMOD_AUTO;
	
	mypfc->c.nmethods = 1;
	mypfc->c.method[0].ordering = CHOLMOD_NATURAL;	
	mypfc->c.postorder = 0;
	
	return mypfc;
}

/* Deallocate memory */
void neCleanup(pfc* mypfc, idxint ncones, idxint l){
	idxint i;
	/* for product-form cholesky */
	for(i = 0; i < l; i++){
		freeSparseMatrix(mypfc->GtG[i]);
	}	
	for(i = 0; i < ncones; i++){
		FREE(mypfc->gte[i]);
		FREE(mypfc->gtw[i]);
		freeSparseMatrix(mypfc->GtG[l+i]);
		freeSparseMatrix(mypfc->G_br[i]);
		FREE(mypfc->wnew[i]);
	}
	if(ncones){
		FREE(mypfc->gte);
		FREE(mypfc->gtw);
		FREE(mypfc->G_br);
		FREE(mypfc->wnew);
	}
	FREE(mypfc->GtG);
	freeSparseMatrix(mypfc->S);
	freeSparseMatrix(mypfc->Spattern);
	
	/* RHS */
	FREE(mypfc->xpGtWinv2z);
	FREE(mypfc->bxbybz);
	
	/* Solution */
	FREE(mypfc->dx);
	FREE(mypfc->dy);
	FREE(mypfc->dz);
	FREE(mypfc->workz);
	
	/* Iterative Refinement */
	FREE(mypfc->ex);
	FREE(mypfc->ey);
	FREE(mypfc->ez);
	FREE(mypfc->ddx);
	FREE(mypfc->ddy);
	FREE(mypfc->ddz);
	
	/* Cholmod stuff */ 
	cholmod_l_free_sparse(&(mypfc->Scm),&(mypfc->c));
	cholmod_l_free_sparse(&(mypfc->Spatterncm),&(mypfc->c));
	cholmod_l_free_sparse(&(mypfc->Scmreg),&(mypfc->c));
	cholmod_l_free_sparse(&(mypfc->Spatterncmreg),&(mypfc->c));
	cholmod_l_free_sparse(&(mypfc->Acm),&(mypfc->c));
	cholmod_l_free_sparse(&(mypfc->Atcm),&(mypfc->c));
	cholmod_l_free_sparse(&(mypfc->Gcm),&(mypfc->c));
	cholmod_l_free_sparse(&(mypfc->RegS),&(mypfc->c));
	cholmod_l_free_sparse(&(mypfc->RegM),&(mypfc->c));
	cholmod_l_free_sparse(&(mypfc->M),&(mypfc->c));
	cholmod_l_free_sparse(&(mypfc->Mreg),&(mypfc->c));
	cholmod_l_free_sparse(&(mypfc->Z),&(mypfc->c));
	cholmod_l_free_sparse(&(mypfc->Zt),&(mypfc->c));
	cholmod_l_free_dense(&(mypfc->xpGtWinv2zcm),&(mypfc->c));
	cholmod_l_free_dense(&(mypfc->RHS),&(mypfc->c));
	cholmod_l_free_dense(&(mypfc->bzcm),&(mypfc->c));
	cholmod_l_free_dense(&(mypfc->up_d),&(mypfc->c));
	cholmod_l_free_dense(&(mypfc->down_d),&(mypfc->c));
	cholmod_l_free_factor(&(mypfc->L),&(mypfc->c));
	cholmod_l_free_factor(&(mypfc->L_M),&(mypfc->c));
	cholmod_l_finish(&(mypfc->c));
	
	FREE(mypfc);
}

/* Compute amount of memory needed to store blockrow of X, row k+1 to (and including) row l+1 */
idxint mem_blockrow(spmat* X, idxint k, idxint l){
	idxint i, j, p, mem_needed;
	mem_needed = 0;
	/* Iterate through columns of X */
	for(j = 0; j < X->n; j++){
		/* Iterate through rows of X */
		for(i = X->jc[j]; i < X->jc[j+1]; i++){
			/* Iterate through rows to be checked: from k to l */
			for(p = k; p <= l; p++){
				/* If row with entry in X equals the current row between k and l, increment mem_needed */
				if(X->ir[i] == p){
					mem_needed++;
					break;
				}
			}
		}
	}
	return mem_needed;
}

/* return blockrow of X, row k+1 to (and including) row l+1. Do this in init-phase! */
spmat* blockrow(spmat* X, idxint k, idxint l){
	idxint i, j, p, flag, nnz, mem_needed;
	/* Compute the memory needed to allocate the resulting matrix */
	mem_needed = mem_blockrow(X,k,l);
	/* Allocate matrix for result */
	spmat* B = newSparseMatrix(l-k+1,X->n,mem_needed);
	/* Number of nonzeros in the resulting blockrow */
	nnz = 0;
	/* Iterate through columns of X */
	for(j = 0; j < X->n; j++){
		/* Flag to check wheter the entry is the first one in the column */
		flag = 0;
		/* Iterate through rows of X */
		for(i = X->jc[j]; i < X->jc[j+1]; i++){
			/* Iterate through rows to be checked */
			for(p = k; p <= l; p++){
				/* Check if there is an entry in X in the rows of interest */
				if(X->ir[i] == p){
					nnz++;
					/* If flag == 0: First entry in this column, thus jc needs an entry */
					if(flag == 0){
						B->jc[j] = nnz-1;
						flag = 1;
					}
					/* Write row and value to ir and pr */
					B->ir[nnz-1] = p-k;
					B->pr[nnz-1] = X->pr[i];	
				}
			}
		}
		/* If there was no entry in this column, the first index of the next column in the blockrow is the current number of nonzeros */
		if(!flag){
			B->jc[j] = nnz;
		}	
	}
	/* Final number of nonzeros in blockrow */
	B->jc[B->n] = nnz;
	B->nnz = nnz;
	return B;
}

/* Return Y = X'*X */
spmat* sparseMtM(spmat* X){
	idxint i, j, k, l, mem, xnnz, ynnz, nnz, flag;
	/* Nonzeros in resulting matrix Y */
	nnz = 0;
	pfloat z;
	/* Compute amount of memory needed to store result */
	mem = count_mem(X);
	/* Allocate memory for result */
	spmat* Y = newSparseMatrix(X->n,X->n,mem);
	/* Iterate through columns of X */
	for(j = 0; j < X->n; j++){
		/* The first index in every column is the current number of nonzeros in result */
		Y->jc[j] = nnz;
		/* Flag is used to check if the entry to be written is the first one in a column */
		flag = 0;
		/* Number of nonzeros in current column */
		xnnz = (X->jc[j+1])-(X->jc[j]);
		if(xnnz != 0){
			/* Write current column in sparse-matrix form to xir, xpr, xjc */
			idxint xir[xnnz];
			pfloat xpr[xnnz];
			for(i = X->jc[j]; i < X->jc[j+1]; i++){
				xir[i-(X->jc[j])] = X->ir[i];
				xpr[i-(X->jc[j])] = X->pr[i];	
			}
			/* Iterate through columns of X */
			for(k = 0; k < X->n; k++){
				/* Number of nonzeros in current column */
				ynnz = (X->jc[k+1]-X->jc[k]);
				if(ynnz != 0){
					/* Write current column in sparse-matrix form to yir, ypr, yjc */
					idxint yir[ynnz];
					pfloat ypr[ynnz];
					for(l = X->jc[k]; l < X->jc[k+1]; l++){
						yir[l-(X->jc[k])] = X->ir[l];
						ypr[l-(X->jc[k])] = X->pr[l];
					}
					/* Compute the dotproduct of the two current columns */
					z = spmat_dotprod(xir,xpr,xnnz,yir,ypr,ynnz);
					if(z != 0){
						/* Write result to Y */
						nnz += 1;
						/* Check if entry is first in column. If not, jc doesnt need an new entry */
						if(!flag){
							Y->jc[j] = nnz-1;
							flag = 1;
						}
						Y->ir[nnz-1] = k;
						Y->pr[nnz-1] = z;
					}
				}
			}
		}
	}
	/* Final number of nonzeros in resulting matrix */
	Y->jc[X->n] = nnz;
	Y->nnz = nnz;
	return Y;
}

/* Computes how much memory is needed to compute X'*X */
idxint count_mem(spmat* X){
	/* If matrix X doesn't have any entries, return 0 */
	if(X->nnz == 0){
		return 0;
	}
	idxint i, j, k, l, sizex, sizey, mem_needed;
	mem_needed = 0;
	/* Iterate through columns */
	for(j = 0; j < X->n; j++){
		/* Number of nonzeros in current column */
		sizex = (X->jc[j+1])-(X->jc[j]);
		/* Check if current column has any entries */
		if(sizex != 0){
			/* Copy current column to vector x (dense representation) */
			idxint x[sizex];
			for(i = X->jc[j]; i < X->jc[j+1]; i++){
				x[i-(X->jc[j])] = X->ir[i];
			}
			/* Iterate through columns */
			for(k = 0; k < X->n; k++){
				/* Number of nonzeros in current column */
				sizey = (X->jc[k+1])-(X->jc[k]);
				/* Check if current column has any entries */
				if(sizey != 0){
					/* Copy current column to vector y (dense representation) */
					idxint y[sizey];
					for(l = X->jc[k]; l < X->jc[k+1]; l++){
						y[l-(X->jc[k])] = X->ir[l];
					}
					/* Check if the two columns are orthogonal. If no, increment mem_needed. */
					if(!is_orthogonal(x,y,sizex,sizey)){
						mem_needed++;
					}
				}
			}
		}
	}
	return mem_needed;
}

/* Computes how much memory is needed to compute X'*X + eye*delta */
idxint count_mem_diag(spmat* X){
	if(X->nnz == 0){
		return 0;
	}
	idxint i, j, k, l, sizex, sizey, mem_needed, mem_diag;
	/* Memory required for all entries resulting from the multiplication */
	mem_needed = 0;
	/* Additional memory required due to adding stuff on diagonal. */
	mem_diag = X->n;
	/* Do the same as in count_mem */
	for(j = 0; j < X->n; j++){
		sizex = (X->jc[j+1])-(X->jc[j]);
		if(sizex != 0){
			idxint x[sizex];
			for(i = X->jc[j]; i < X->jc[j+1]; i++){
				x[i-(X->jc[j])] = X->ir[i];
			}
			for(k = 0; k < X->n; k++){
				sizey = (X->jc[k+1])-(X->jc[k]);
				if(sizey != 0){
					idxint y[sizey];
					for(l = X->jc[k]; l < X->jc[k+1]; l++){
						y[l-(X->jc[k])] = X->ir[l];
					}
					if(!is_orthogonal(x,y,sizex,sizey)){
						mem_needed++;
						/* If an entry on the diagonal results from the multiplication, decrement the additional memory needed for regularization */
						if(j == k){
							mem_diag--;
						}
					}
				}
			}
		}
	}
	/* Return the total memory needed */
	return mem_needed+mem_diag;
}

/* Symbolic factorization of S. Future work: do the same for M. */
void initfactors(pfc* mypfc, cone* C){
	idxint i;
	/* Compute Spattern */
    mypfc->Spattern = sparseMtM(mypfc->G);
    /* Copy Spattern to cholmod format */
    for(i = 0; i <= mypfc->Spattern->n; i++){
		((idxint*)mypfc->Spatterncm->p)[i] = mypfc->Spattern->jc[i];
	}
	for(i = 0; i < mypfc->Spattern->nnz; i++){
		((idxint*)mypfc->Spatterncm->i)[i] = mypfc->Spattern->ir[i];
		((pfloat*)mypfc->Spatterncm->x)[i] = mypfc->Spattern->pr[i];
	}
	/* Add ones on diagonal */
	pfloat alpha[2] = {1,0};
	pfloat beta[2] = {1,0};
	mypfc->Spatterncmreg = cholmod_l_add(mypfc->Spatterncm,mypfc->RegS,alpha,beta,1,1,&(mypfc->c));
	mypfc->Spatterncmreg->stype = 1;
	
	/* Pattern only */
	cholmod_l_sparse_xtype(CHOLMOD_PATTERN,mypfc->Spatterncmreg,&(mypfc->c));
	
	/* Analyze S */
	mypfc->L = cholmod_l_analyze(mypfc->Spatterncmreg,&(mypfc->c));	
	
}

/********** FACTOR & SOLVE **********/

/* Compute xpGtWinvbz (a part of the RHS) */
void xpGtWinv2z(pfc* mypfc, cone* C, idxint isItRef){
	idxint i;
	/* Temporarily needed vetors */
	pfloat temp1[mypfc->G->m];
	pfloat temp2[mypfc->G->m];
	
	/* Check if the function was called for iterative refinement or not */
	if(isItRef){
		/* Compute temp1 = W^(-1)*ez */
		unscale(mypfc->ez,C,temp1);
	}
	else{
		/* Compute temp1 = W^(-1)*bz */
		unscale(mypfc->bz,C,temp1);
	}
	/* Compute temp2 = W^(-1)*temp1 = W^(-2)*ez or W^(-2)*bz */
	unscale(temp1,C,temp2);
	/* Multiply temp2 with G transposed and write it to mypfc->xpGtWinv2z */
	sparseMtv(mypfc->G,temp2,mypfc->xpGtWinv2z);
	/* Check if the function was called for iterative refinement or not */
	if(isItRef){
		/* Add ex to xpGtWinv2z */
		vadd(mypfc->G->n,mypfc->ex,mypfc->xpGtWinv2z);
	}
	else{
		/* Add bx to xpGtWinv2z */
		vadd(mypfc->G->n,mypfc->bx,mypfc->xpGtWinv2z);
	}
	/* Copy it to cholmod format */
	for(i = 0; i < mypfc->G->n; i++){
		((pfloat*)mypfc->xpGtWinv2zcm->x)[i] = mypfc->xpGtWinv2z[i];
	}
}

/* compute G'_i*w_i and G'_i*e0 */
void computeUpdates(pfc* mypfc, cone* C){
	idxint i, j;
	/* Change scaling to the scaling needed to compute the updates and write it to mypfc->wnew */
	change_scaling(C,mypfc->wnew);
	for(i = 0; i < C->nsoc; i++){
		/* e: Unit vector of appropriate dimension scaled by sqrt(2)/eta */
		pfloat e[C->soc[i].p];
		for(j = 0; j < C->soc[i].p; j++){
			e[j] = 0;
		}
		e[0] = SAFEDIV_POS(sqrt(2),C->soc[i].eta);
		/* Divide the new scalings by eta/sqrt(2) */
		vecDiv(C->soc[i].eta/sqrt(2),C->soc[i].p,mypfc->wnew[i]);
		/* Multiply the blockrows of G and the according scaling-vectors and write it to mypfc->gtw */
		sparseMtv(mypfc->G_br[i],mypfc->wnew[i],mypfc->gtw[i]);
		/* Multiply the blockrows of G and the according scaled unit vectors and write it to mypfc->gte */
		sparseMtv(mypfc->G_br[i],e,mypfc->gte[i]);		
	}	
}

/* Change scaling representation for product form cholesky */
void change_scaling(cone* C, pfloat** w_new){
	idxint i, j;
	/* SOC-cones (scaling stays the same for LP-cone) */
	for(i = 0; i < C->nsoc; i++){		
		pfloat a = C->soc[i].a; /* Old wbar(1) */
		pfloat w = C->soc[i].w; /* Od q'*q, where q = wbar(2:end) */ 
		pfloat* q = C->soc[i].q; /* Old q = wbar(2:end) */ 
		idxint sizeq = (C->soc[i].p)-1; /* Size of q */ 		
		pfloat aq[sizeq]; 
		pfloat qw[sizeq];		 
		
		/* Compute new wbar(1) */
		w_new[i][0] = sqrt(0.5*(a*a+w+1));
		/* Compute new wbar(2:end) = 1/(2*w_new(1))*(-wbar(1)*wbar(2:end)-(I+1/(1+wbar(1))*wbar(2:end)*wbar(2:end)^T)*wbar(2:end)) */
		for(j = 0; j < sizeq; j++){
			aq[j] = -a*q[j]/(2*w_new[i][0]);
			qw[j] = w/(1+a)*q[j]+q[j];
		}
		vsubscale(sizeq,1/(2*w_new[i][0]),qw,aq);
		for(j = 0; j < C->soc[i].p-1; j++){
			w_new[i][j+1] = aq[j];
		}
	}	
}

/* Add the scaled G_i'*G_i matrices together */
void addS(pfc* mypfc, cone* C){
	idxint i;
	/* Iterate through LP-cones */
   	for(i = 0; i < C->lpc->p; i++){
   		/* Scale current G_i'*G_i */            
        sparseDiv(C->lpc->v[i],mypfc->GtG[i]);
        /* Add it to S */
        sparseAdd(mypfc->GtG[i],mypfc->S);
        /* Scale current G_i'*G_i back, so it can be used again in the next step */
        sparseDiv(1/(C->lpc->v[i]),mypfc->GtG[i]);        
    }
    /* Iterate through SOC-cones */
    for(i = 0; i < C->nsoc; i++){
    	/* Scale current G_i'*G_i */ 
        sparseDiv(C->soc[i].eta_square,mypfc->GtG[i+C->lpc->p]);
        /* Add it to S */
        sparseAdd(mypfc->GtG[i+C->lpc->p],mypfc->S);
        /* Scale current G_i'*G_i back, so it can be used again in the next step */
        sparseDiv(1/(C->soc[i].eta_square),mypfc->GtG[i+C->lpc->p]);
    }
}

/* Regularize, then factor S = sum((1/eta^2_i)*G'_i*G_i) */
void factorS(pfc* mypfc){
	idxint i;
	/* Copy S to cholmod format */	
	for(i = 0; i <= mypfc->S->n; i++){
		((idxint*)mypfc->Scm->p)[i] = mypfc->S->jc[i];
	}
	for(i = 0; i < mypfc->S->nnz; i++){
		((idxint*)mypfc->Scm->i)[i] = mypfc->S->ir[i];
		((pfloat*)mypfc->Scm->x)[i] = mypfc->S->pr[i];
	}
	/* Vectors needed for cholmod_add. mypfc->delta: regularization parameter */
	pfloat alpha[2] = {1,0};
	pfloat beta[2] = {mypfc->delta,0};
	/* Regularize S: Add I*delta to diagonal */
	mypfc->Scmreg = cholmod_l_add(mypfc->Scm,mypfc->RegS,alpha,beta,1,1,&(mypfc->c));
	/* Tell cholmod that it is a symmetric matrix */
	mypfc->Scmreg->stype = 1;
	/* Numeric factorization of regularized S */
	cholmod_l_factorize(mypfc->Scmreg,mypfc->L,&(mypfc->c));
}
	

/* Up- and downdates on factor L */
void updown(pfc* mypfc){
	idxint i, j;
	/* Pointers to sparse vectors for up- and downdates. Cholmod needs sparse input for updown */
	cholmod_sparse* up;
	cholmod_sparse* down;
	/* Iterate through SOC-cones */	
	for(i = 0; i < mypfc->ncones; i++){
		/* Write up- and downdates to cholmod-dense format */
		for(j = 0; j < mypfc->S->m; j++){
			((pfloat*)mypfc->up_d->x)[j] = mypfc->gtw[i][j];
			((pfloat*)mypfc->down_d->x)[j] = mypfc->gte[i][j];
		}
		/* Convert the dense up- and downdates to cholmod-sparse format (needed input) */
		up = cholmod_l_dense_to_sparse(mypfc->up_d,1,&(mypfc->c));
		down = cholmod_l_dense_to_sparse(mypfc->down_d,1,&(mypfc->c));
		/* Update factor L */
		cholmod_l_updown(1,up,mypfc->L,&(mypfc->c));
		/* Downdate factor L */
		cholmod_l_updown(0,down,mypfc->L,&(mypfc->c));
		/* Free the memory allocated by conversion to sparse cholmod format */
		cholmod_l_free_sparse(&up,&(mypfc->c));
		cholmod_l_free_sparse(&down,&(mypfc->c));
	}	
	/* convert to LL' instead of LDL', needed for computation of M */
	cholmod_l_change_factor(CHOLMOD_REAL,1,0,1,1,mypfc->L,&(mypfc->c));
}

/* compute Z and M, L*Z = A', Z'*Z = M = A*(G'*W^(-2)*G)^(-1)*A' */
void compZM(pfc* mypfc){
	/* Compute Z = A'\L in cholmod format */
	mypfc->Z = cholmod_l_spsolve(4,mypfc->L,mypfc->Atcm,&(mypfc->c));
	/* Compute Z transposed in cholmod format */
	mypfc->Zt = cholmod_l_transpose(mypfc->Z,1,&(mypfc->c));
	/* Compute M = Z'*Z in cholmod format */
	mypfc->M = cholmod_l_ssmult(mypfc->Zt,mypfc->Z,1,1,1,&(mypfc->c));
}

/* Regularize, then factor M = Z'*Z = A*(G'*W^(-2)*G)^(-1)*A' */
void factorM(pfc* mypfc){
	/* Vectors needed for cholmod_add. mypfc->delta: regularization parameter */
	pfloat alpha[2] = {1,0};
	pfloat beta[2] = {mypfc->delta,0};
	/* Regularize M: Add I*delta to diagonal */
	mypfc->Mreg = cholmod_l_add(mypfc->M,mypfc->RegM,alpha,beta,1,1,&(mypfc->c));
	/* Tell cholmod that this is a symmetric matrix */
	mypfc->Mreg->stype = 1;
	/* Symbolic factorization of M. FUTURE WORK: Do this in init-phase (needed only once) */
	mypfc->L_M = cholmod_l_analyze(mypfc->Mreg,&(mypfc->c));
	/* Numeric factorization of regularized M */
	cholmod_l_factorize(mypfc->Mreg,mypfc->L_M,&(mypfc->c));
}

/* Compute RHS of normal equations form. RHS = A*(delta*I+G'W^(-2)*G)^(-1)*(bx+G'*W^(-2)*bz)-by */
void RHS(pfc* mypfc, cone* C, idxint isItRef){
	idxint i;
	/* Check if function is being called in iterative refinement or not */
	if(isItRef){
		/* Copy mypfc->ey to RHS */
		for(i = 0; i < mypfc->A->m; i++){
			((pfloat*)mypfc->RHS->x)[i] = mypfc->ey[i];
		}	
	}
	else{
		/* Copy mypfc->by to RHS */
		for(i = 0; i < mypfc->A->m; i++){
			((pfloat*)mypfc->RHS->x)[i] = mypfc->by[i];
		}
	}
	/* Compute bxpGtWinv2bz */
	xpGtWinv2z(mypfc,C,isItRef);
	
	/* Compute RHS in cholmod format*/
	mypfc->RHStemp = cholmod_l_solve(1,mypfc->L,mypfc->xpGtWinv2zcm,&(mypfc->c));	
	pfloat alpha[2] = {1,0};
	pfloat beta[2] = {-1,0};
	cholmod_l_sdmult(mypfc->Acm,0,alpha,beta,mypfc->RHStemp,mypfc->RHS,&(mypfc->c));
	cholmod_l_free_dense(&(mypfc->RHStemp),&(mypfc->c));
}

/* Solve */
void LinSyssolve(pfc* mypfc, cone* C, idxint isItRef){
	idxint i;
	
	/* Solve for dy. L_M*L_M'*dy = RHS */ 
	mypfc->worky = cholmod_l_solve(1,mypfc->L_M,mypfc->RHS,&(mypfc->c));
	
	/* Solve for dx. L*L'*dx = bx+G'*W^(-2)*bz-A'*dy */
	pfloat alpha[2] = {-1,0};
	pfloat beta[2] = {1,0};
	cholmod_l_sdmult(mypfc->Acm,1,alpha,beta,mypfc->worky,mypfc->xpGtWinv2zcm,&(mypfc->c));
	mypfc->workx = cholmod_l_solve(1,mypfc->L,mypfc->xpGtWinv2zcm,&(mypfc->c));

	/* Solve for dz. dz = W^(-2)*(G*dx-bz) */
	/* Check if function is called as iterative refinement */
	if(isItRef){
		/* If yes, use ez (copy to cholmod format) */
		for(i = 0; i < mypfc->G->m; i++){
			((pfloat*)mypfc->bzcm->x)[i] = mypfc->ez[i];
		}
	}
	else{
		/* Otherwise, use bz (copy to cholmod format) */
		for(i = 0; i < mypfc->G->m; i++){
			((pfloat*)mypfc->bzcm->x)[i] = mypfc->bz[i];
		}
	}
	pfloat* bztemp = mypfc->bzcm->x;
	cholmod_l_sdmult(mypfc->Gcm,0,beta,alpha,mypfc->workx,mypfc->bzcm,&(mypfc->c));
	pfloat temp[mypfc->G->m];
	unscale(bztemp,C,temp);
	unscale(temp,C,mypfc->workz);
	
	/* Check function is called for iterative refinement. Depending on that, write result to dd... or d... */
	if(isItRef){
		for(i = 0; i < mypfc->A->m; i++){
			mypfc->ddy[i] = ((pfloat*)mypfc->worky->x)[i];
		}
		for(i = 0; i < mypfc->A->n; i++){
			mypfc->ddx[i] =((pfloat*)mypfc->workx->x)[i];
		}
		for(i = 0; i < mypfc->G->m; i++){
			mypfc->ddz[i] = mypfc->workz[i];
		}
			
	}
	else{
		for(i = 0; i < mypfc->A->m; i++){
			mypfc->dy[i] = ((pfloat*)mypfc->worky->x)[i];
		}
		for(i = 0; i < mypfc->A->n; i++){
			mypfc->dx[i] = ((pfloat*)mypfc->workx->x)[i];
		}
		for(i = 0; i < mypfc->G->m; i++){
			mypfc->dz[i] = mypfc->workz[i];
		}		
	}
	/* Free space allocated by cholmod_solve */
	cholmod_l_free_dense(&(mypfc->worky),&(mypfc->c));
	cholmod_l_free_dense(&(mypfc->workx),&(mypfc->c));
}

/* Iterative refinement */
idxint itref(pfc* mypfc, cone* C){
	idxint i, j;
	pfloat nex, ney, nez, nerr;
	/* Maximum number of iterative refinement steps */
	pfloat nItref = 3;
	
	/* Concatenate bx, by and bz into one vector bxbybz */
	for(i = 0; i < mypfc->A->n; i++){
		mypfc->bxbybz[i] = mypfc->bx[i];
	}
	for(i = 0; i < mypfc->A->m; i++){
		mypfc->bxbybz[mypfc->A->n+i] = mypfc->by[i];
	}
	for(i = 0; i < mypfc->G->m; i++){
		mypfc->bxbybz[mypfc->A->n+mypfc->A->m+i] = mypfc->bz[i];
	}
	/* Compute norm to check termination criterion of the iterative refinement */
	pfloat bnorm = 1+norminf(mypfc->bxbybz,mypfc->bxbybzsize);
	for(i = 0; i < nItref; i++){
		/* Comput the errors on the solve */
		
		/* Error on ex. ex = bx - A'*dy - G'*dz (- I*delta*dx). The regularization is neglected in the computation of the error */
		for(j = 0; j < mypfc->A->n; j++){
			mypfc->ex[j] = mypfc->bx[j];
			/* If the regularization is not to be neglected in the computation of the error, uncomment the next line */
			/* mypfc->ex[j] -= mypfc->delta*mypfc->dx[j]; */
		}
		sparseMtVm(mypfc->A,mypfc->dy,mypfc->ex,0,0);
		sparseMtVm(mypfc->G,mypfc->dz,mypfc->ex,0,0);
		
		/* Error on ey. ey = by - A*dx (+I*delta*dy). The regularization is neglected in the computation of the error */
		for(j = 0; j < mypfc->A->m; j++){
			mypfc->ey[j] = mypfc->by[j];
			/* If the regularization is not to be neglected in the computation of the error, uncomment the next line */
			/* mypfc->ey[j] += mypfc->delta*mypfc->dy[j]; */
		}
		sparseMV(mypfc->A,mypfc->dx,mypfc->ey,-1,0);
		
		/* Error on ez. ez = bz - G*dx + W^2*dz */
		for(j = 0; j < mypfc->G->m; j++){
			mypfc->ez[j] = mypfc->bz[j];
		}
		sparseMV(mypfc->G,mypfc->dx,mypfc->ez,-1,0);
		pfloat temp1[mypfc->G->m];
		pfloat temp2[mypfc->G->m];
		scale(mypfc->dz,C,temp1);
		scale(temp1,C,temp2);
		vadd(mypfc->G->m,temp2,mypfc->ez);		
		
		/* Maximum errors (infinity norm) */
		nex = norminf(mypfc->ex,mypfc->A->n);
		ney = norminf(mypfc->ey,mypfc->A->m);
		nez = norminf(mypfc->ez,mypfc->G->m);
		nerr = MAX(nex,ney);
		nerr = MAX(nerr,nez);
		
		/* Continue? */
		if(nerr < LINSYSACC*bnorm){
			break;
		}
		
		/* Compute new RHS */
		RHS(mypfc,C,1);
		
		/* Solve new system */ 
		LinSyssolve(mypfc,C,1);
		
		/* Add to solution */
		vadd(mypfc->A->n,mypfc->ddx,mypfc->dx);
		vadd(mypfc->A->m,mypfc->ddy,mypfc->dy);
		vadd(mypfc->G->m,mypfc->ddz,mypfc->dz);
	}
	return i;
	
}

/* Factor */
void NEfactor(pfc* mypfc, cone* C){
	initfactors(mypfc,C);
	computeUpdates(mypfc,C);
	addS(mypfc,C);
	tic(&mypfc->tnefactor);
	factorS(mypfc);
	updown(mypfc);
	compZM(mypfc);
	factorM(mypfc);
	mypfc->tfactor = toc(&mypfc->tnefactor);
}

/* Solve */
idxint NEsolve(pfc* mypfc,cone* C, pfloat* bx, pfloat* by, pfloat* bz){
	idxint nit;
	mypfc->bx = bx;
	mypfc->by = by;
	mypfc->bz = bz;
	RHS(mypfc,C,0);
	tic(&mypfc->tnesolve);
	LinSyssolve(mypfc,C,0);
	mypfc->tsolve = toc(&mypfc->tnesolve);
	nit = itref(mypfc,C);
	return nit;
}

/********** DEBUG **********/

/* print sparse matrix */
void printSparse(spmat* Y){
	idxint i;
	printf("jc: ");
	for(i = 0; i <= Y->n; i++){
		printf("%i ",Y->jc[i]);
	}
	printf("\nir: ");
	for(i = 0; i < Y->nnz; i++){
		printf("%i ",Y->ir[i]);
	}
	printf("\npr: ");
	for(i = 0; i < Y->nnz; i++){
		printf("%f ",Y->pr[i]);
	}
	printf("\n");
}

/* print sparse cholmod matrix */
void printSparseCM(cholmod_sparse* Y,cholmod_common* c){
	idxint Ynnz = cholmod_l_nnz(Y,c);
	idxint Ycol = Y->ncol;
	idxint* jc = Y->p;
	idxint* ir = Y->i;
	pfloat* pr = Y->x;
	
	idxint i;
	printf("jc: ");
	for(i = 0; i <= Ycol; i++){
		printf("%i ",jc[i]);
	}
	printf("\nir: ");
	for(i = 0; i < Ynnz; i++){
		printf("%i ",ir[i]);
	}
	printf("\npr: ");
	for(i = 0; i < Ynnz; i++){
		printf("%f ",pr[i]);
	}
	printf("\n");
}