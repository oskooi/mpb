#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <config.h>
#include <check.h>

#include "maxwell.h"

/* This file is has too many #ifdef's...blech. */

#define MIN2(a,b) ((a) < (b) ? (a) : (b))

maxwell_data *create_maxwell_data(int nx, int ny, int nz,
				  int *local_N, int *N_start, int *alloc_N,
				  int num_bands,
				  int num_fft_bands)
{
     int n[3] = {nx, ny, nz}, rank = (nz == 1) ? (ny == 1 ? 1 : 2) : 3, i;
     maxwell_data *d = 0;
     int fft_data_size;

#ifndef HAVE_FFTW
#  error Non-FFTW FFTs are not currently supported.
#endif
     
#ifdef HAVE_FFTW
     CHECK(sizeof(fftw_real) == sizeof(real),
	   "floating-point type is inconsistent with FFTW!");
#endif

     d = (maxwell_data *) malloc(sizeof(maxwell_data));
     CHECK(d, "out of memory");

     d->nx = nx;
     d->ny = ny;
     d->nz = nz;
     
     d->num_fft_bands = MIN2(num_bands, num_fft_bands);

     d->current_k[0] = d->current_k[1] = d->current_k[2] = 0.0;
     d->polarization = NO_POLARIZATION;

     d->last_dim = n[rank - 1];

     /* ----------------------------------------------------- */
#ifndef HAVE_MPI 
     d->local_nx = nx; d->local_ny = ny;
     d->local_x_start = d->local_y_start = 0;
     *local_N = *alloc_N = nx * ny * nz;
     *N_start = 0;
     d->other_dims = *local_N / d->last_dim;

     d->fft_data = 0;  /* initialize it here for use in specific planner? */

#  ifdef HAVE_FFTW
#    ifdef SCALAR_COMPLEX
     d->fft_output_size = fft_data_size = nx * ny * nz;
     d->plan = fftwnd_create_plan_specific(rank, n, FFTW_FORWARD,
					   FFTW_ESTIMATE | FFTW_IN_PLACE,
					   (FFTW_COMPLEX*) d->fft_data,
					   3 * num_fft_bands,
					   (FFTW_COMPLEX*) d->fft_data,
					   3 * num_fft_bands);
     d->iplan = fftwnd_create_plan_specific(rank, n, FFTW_BACKWARD,
					    FFTW_ESTIMATE | FFTW_IN_PLACE,
					    (FFTW_COMPLEX*) d->fft_data,
					    3 * num_fft_bands,
					    (FFTW_COMPLEX*) d->fft_data,
					    3 * num_fft_bands);
#    else /* not SCALAR_COMPLEX */
     d->fft_output_size = fft_data_size = 
	  d->other_dims * 2 * (d->last_dim / 2 + 1);
     d->plan = rfftwnd_create_plan(rank, n, FFTW_FORWARD,
				   FFTW_ESTIMATE | FFTW_IN_PLACE,
				   REAL_TO_COMPLEX);
     d->plan = rfftwnd_create_plan(rank, n, FFTW_BACKWARD,
				   FFTW_ESTIMATE | FFTW_IN_PLACE,
				   COMPLEX_TO_REAL);
#    endif /* not SCALAR_COMPLEX */
#  endif /* HAVE_FFTW */

#else /* HAVE_MPI */
     /*----------------------------------------------------- */

#  ifdef HAVE_FFTW

     CHECK(rank > 1, "rank < 2 MPI computations are not supported");

#    ifdef SCALAR_COMPLEX
     d->plan = fftwnd_mpi_create_plan(MPI_COMM_WORLD, rank, n,
				      FFTW_FORWARD,
				      FFTW_ESTIMATE | FFTW_IN_PLACE);
     d->iplan = fftwnd_mpi_create_plan(MPI_COMM_WORLD, rank, n,
				       FFTW_BACKWARD,
				       FFTW_ESTIMATE | FFTW_IN_PLACE);

     fftwnd_mpi_local_sizes(plan, &d->local_nx, &d->local_x_start,
			    &d->local_ny, &d->local_y_start,
			    &fft_data_size);
     
     d->fft_output_size = nx * d->local_ny * nz;

#    else /* not SCALAR_COMPLEX */

     CHECK(rank > 2, "rank <= 2 MPI computations must use SCALAR_COMPLEX");

#  error rfftw MPI transforms not yet supported

#    endif /* not SCALAR_COMPLEX */
     
     *local_N = d->local_nx * ny * nz;
     *N_start = d->local_x_start * ny * nz;
     *alloc_N = *local_N;
     d->other_dims = *local_N / d->last_dim;

#  endif /* HAVE_FFTW */

#endif /* HAVE_MPI */
     /* ----------------------------------------------------- */

#ifdef HAVE_FFTW
     CHECK(d->plan && d->iplan, "FFTW plan creation failed");
#endif

     d->eps_inv = (symmetric_matrix*) malloc(sizeof(symmetric_matrix)
	                                     * d->fft_output_size);
     CHECK(d->eps_inv, "out of memory");

     /* a scratch output array is required because the "ordinary" arrays
	are not in a cartesian basis (or even a constant basis). */
     d->fft_data = (scalar*) malloc(sizeof(scalar) * 3
				    * num_fft_bands * fft_data_size);
     CHECK(d->fft_data, "out of memory");

     d->k_plus_G = (k_data*) malloc(sizeof(k_data) * *local_N);
     d->k_plus_G_normsqr = (real*) malloc(sizeof(real) * *local_N);
     CHECK(d->k_plus_G && d->k_plus_G_normsqr, "out of memory");

     d->eps_inv_mean = 1.0;

     d->local_N = *local_N;
     d->N_start = *N_start;
     d->alloc_N = *alloc_N;
     d->num_bands = num_bands;
     d->N = nx * ny * nz;

     return d;
}

void destroy_maxwell_data(maxwell_data *d)
{
     if (d) {

#ifdef HAVE_FFTW
#  ifdef HAVE_MPI
	  fftwnd_mpi_destroy_plan(d->plan);
	  fftwnd_mpi_destroy_plan(d->iplan);
#  else /* not HAVE_MPI */
#    ifdef SCALAR_COMPLEX
	  fftwnd_destroy_plan(d->plan);
	  fftwnd_destroy_plan(d->iplan);
#    else /* not SCALAR_COMPLEX */
	  rfftwnd_destroy_plan(d->plan);
	  rfftwnd_destroy_plan(d->iplan);
#    endif /* not SCALAR_COMPLEX */
#  endif /* not HAVE_MPI */
#endif /* HAVE FFTW */

	  free(d->eps_inv);
	  free(d->fft_data);
	  free(d->k_plus_G);
	  free(d->k_plus_G_normsqr);

	  free(d);
     }
}

/* compute a = b x c */
static void compute_cross(real *a0, real *a1, real *a2,
			  real b0, real b1, real b2,
			  real c0, real c1, real c2)
{
     *a0 = b1 * c2 - b2 * c1;
     *a1 = b2 * c0 - b0 * c2;
     *a2 = b0 * c1 - b1 * c0;
}

void update_maxwell_data_k(maxwell_data *d, real k[3],
			   real G1[3], real G2[3], real G3[3])
{
     int nx = d->nx, ny = d->ny, nz = d->nz;
     int cx = d->nx/2, cy = d->ny/2, cz = d->nz/2;
     k_data *kpG = d->k_plus_G;
     real *kpGn2 = d->k_plus_G_normsqr;
     int x, y, z;
     real kx = k[0], ky = k[1], kz = k[2];

     if (kx == 0.0 && ky == 0.0 && kz == 0.0) {
	  printf("detected zero k\n");
	  kx = 1e-5;
     }

     d->current_k[0] = kx;
     d->current_k[1] = ky;
     d->current_k[2] = kz;

     /* make sure current polarization is still valid: */
     set_maxwell_data_polarization(d, d->polarization);

     for (x = d->local_x_start; x < d->local_x_start + d->local_nx; ++x) {
	  int kxi = (x > cx) ? (x - nx) : x;
	  for (y = 0; y < ny; ++y) {
	       int kyi = (y > cy) ? (y - ny) : y;
	       for (z = 0; z < nz; ++z, kpG++, kpGn2++) {
		    int kzi = (z > cz) ? (z - nz) : z;
		    real kpGx, kpGy, kpGz, a, b, c, leninv;

		    /* Compute k+G: */
		    kpGx = kx + G1[0]*kxi + G2[0]*kyi + G3[0]*kzi;
		    kpGy = ky + G1[1]*kxi + G2[1]*kyi + G3[1]*kzi;
		    kpGz = kz + G1[2]*kxi + G2[2]*kyi + G3[2]*kzi;

		    a = kpGx*kpGx + kpGy*kpGy + kpGz*kpGz;
		    kpG->kmag = sqrt(a);
		    *kpGn2 = a;
		    
		    /* Now, compute the two normal vectors: */

		    if (kpGx == 0.0 && kpGy == 0.0) {
			 /* just put n in the x direction if k+G is in z: */
			 kpG->nx = 1.0;
			 kpG->ny = 0.0;
			 kpG->nz = 0.0;
		    }
		    else {
			 /* otherwise, let n = z x (k+G), normalized: */
			 compute_cross(&a, &b, &c,
				       0.0, 0.0, 1.0,
				       kpGx, kpGy, kpGz);
			 leninv = 1.0 / sqrt(a*a + b*b + c*c);
			 kpG->nx = a * leninv;
			 kpG->ny = b * leninv;
			 kpG->nz = c * leninv;
		    }

		    /* m = n x (k+G), normalized */
		    compute_cross(&a, &b, &c,
				  kpG->nx, kpG->ny, kpG->nz,
				  kpGx, kpGy, kpGz);
		    leninv = 1.0 / sqrt(a*a + b*b + c*c);
		    kpG->mx = a * leninv;
		    kpG->my = b * leninv;
		    kpG->mz = c * leninv;

#ifdef DEBUG
#define DOT(u0,u1,u2,v0,v1,v2) ((u0)*(v0) + (u1)*(v1) + (u2)*(v2))

		    /* check orthogonality */
		    CHECK(fabs(DOT(kpGx, kpGy, kpGz,
				   kpG->nx, kpG->ny, kpG->nz)) < 1e-6,
			  "vectors not orthogonal!");
		    CHECK(fabs(DOT(kpGx, kpGy, kpGz,
				   kpG->mx, kpG->my, kpG->mz)) < 1e-6,
			  "vectors not orthogonal!");
		    CHECK(fabs(DOT(kpG->mx, kpG->my, kpG->mz,
				   kpG->nx, kpG->ny, kpG->nz)) < 1e-6,
			  "vectors not orthogonal!");

		    /* check normalization */
		    CHECK(fabs(DOT(kpG->nx, kpG->ny, kpG->nz,
				   kpG->nx, kpG->ny, kpG->nz) - 1.0) < 1e-6,
			  "vectors not unit vectors!");
		    CHECK(fabs(DOT(kpG->mx, kpG->my, kpG->mz,
				   kpG->mx, kpG->my, kpG->mz) - 1.0) < 1e-6,
			  "vectors not unit vectors!");
#endif
	       }
	  }
     }
}

void set_maxwell_data_polarization(maxwell_data *d,
				   polarization_t polarization)
{
     if (d->current_k[2] != 0.0 || d->nz != 1)
	  polarization = NO_POLARIZATION;
     d->polarization = polarization;
}

maxwell_target_data *create_maxwell_target_data(maxwell_data *md, 
						real target_frequency)
{
     maxwell_target_data *d;

     d = (maxwell_target_data *) malloc(sizeof(maxwell_target_data));
     CHECK(d, "out of memory");

     d->d = md;
     d->target_frequency = target_frequency;

     d->T = create_evectmatrix(md->N, 2, md->num_bands, 
			       md->local_N, md->N_start, md->alloc_N);

     return d;
}

void destroy_maxwell_target_data(maxwell_target_data *d)
{
     if (d) {
	  destroy_evectmatrix(d->T);
	  free(d);
     }
}

/* Set Vinv = inverse of V, where both V and Vinv are symmetric matrices. */
static void sym_matrix_invert(symmetric_matrix *Vinv, symmetric_matrix V)
{
     double detinv;
     
     /* compute the determinant: */
     detinv = V.m00*V.m11*V.m22 - V.m02*V.m11*V.m02 +
	      2.0 * V.m01*V.m12*V.m02 -
	      V.m01*V.m01*V.m22 - V.m12*V.m12*V.m00;

     /* don't bother to check for singular matrices, as that shouldn't
	be possible in the context in which this is used */
     detinv = 1.0/detinv;
     
     Vinv->m00 = detinv * (V.m11*V.m22 - V.m12*V.m12);
     Vinv->m11 = detinv * (V.m00*V.m22 - V.m02*V.m02);
     Vinv->m22 = detinv * (V.m11*V.m00 - V.m01*V.m01);
     
     Vinv->m02 = detinv * (V.m01*V.m12 - V.m11*V.m02);
     Vinv->m01 = -detinv * (V.m01*V.m22 - V.m12*V.m02);
     Vinv->m12 = -detinv * (V.m00*V.m12 - V.m01*V.m02);
}

#define SMALL 1.0e-6

void set_maxwell_dielectric(maxwell_data *md,
			    int mesh_size[3],
			    real R1[3], real R2[3], real R3[3],
			    dielectric_function epsilon,
			    void *epsilon_data)
{
     real s1[3], s2[3], s3[3];  /* grid step vectors */
     real m1[3], m2[3], m3[3];  /* mesh step vectors */
     real mesh_center[3];
     int i, j, k, mi, mj, mk;
     int mesh_prod = mesh_size[0] * mesh_size[1] * mesh_size[2];
     real eps_inv_total = 0.0;
     int nx, ny, nz, local_nx, local_x_start, local_x_end;

     nx = md->nx; ny = md->ny; nz = md->nz;
     local_nx = md->local_N / (ny * nz);
     local_x_start = md->N_start / (ny * nz);

     {
	  int mesh_divisions[3];

	  for (i = 0; i < 3; ++i) {
	       mesh_divisions[i] = mesh_size[i] <= 1 ? 1 : mesh_size[i] - 1;
	       mesh_center[i] = (mesh_size[i] - 1) * 0.5;
	  }

	  for (i = 0; i < 3; ++i) {
	       s1[i] = R1[i] / nx;
	       s2[i] = R2[i] / ny;
	       s3[i] = R3[i] / nz;
	       m1[i] = s1[i] / mesh_divisions[0];
	       m2[i] = s2[i] / mesh_divisions[1];
	       m3[i] = s3[i] / mesh_divisions[2];
	  }
     }

     for (i = 0; i < local_nx; ++i)
	  for (j = 0; j < ny; ++j)
	       for (k = 0; k < nz; ++k) {
		    real eps_mean = 0.0, eps_inv_mean = 0.0, norm_len;
		    real R[3], norm[3] = { 0.0, 0.0, 0.0 };
		    int ijk, i2 = i + local_x_start;

		    R[0] = i2 * s1[0] + j * s2[0] + k * s3[0];
		    R[1] = i2 * s1[1] + j * s2[1] + k * s3[1];
		    R[2] = i2 * s1[2] + j * s2[2] + k * s3[2];

		    for (mi = 0; mi < mesh_size[0]; ++mi)
			 for (mj = 0; mj < mesh_size[1]; ++mj)
			      for (mk = 0; mk < mesh_size[2]; ++mk) {
				   real del[3], r[3], eps;

				   del[0] = (mi - mesh_center[0])*m1[0] 
					  + (mj - mesh_center[1])*m2[0]
					  + (mk - mesh_center[2])*m3[0];
				   r[0] = R[0] + del[0];
				   del[1] = (mi - mesh_center[0])*m1[1] 
					  + (mj - mesh_center[1])*m2[1]
					  + (mk - mesh_center[2])*m3[1];
				   r[1] = R[1] + del[1];
				   del[2] = (mi - mesh_center[0])*m1[2] 
					  + (mj - mesh_center[1])*m2[2]
					  + (mk - mesh_center[2])*m3[2];
				   r[2] = R[2] + del[2];

				   eps = epsilon(r, epsilon_data);

				   eps_mean += eps;
				   eps_inv_mean += 1.0 / eps;

				   norm[0] += eps * del[0];
				   norm[1] += eps * del[1];
				   norm[2] += eps * del[2];
			      }
		    
		    eps_mean = eps_mean / mesh_prod;
		    eps_inv_mean = mesh_prod / eps_inv_mean;

		    norm_len = sqrt(norm[0] * norm[0] +
				    norm[1] * norm[1] +
				    norm[2] * norm[2]);

		    ijk = (i * ny + j) * nz + k;
		    
		    if (norm_len > SMALL &&
			fabs(eps_mean - eps_inv_mean) > SMALL) {
			 symmetric_matrix eps;

			 norm[0] /= norm_len;
			 norm[1] /= norm_len;
			 norm[2] /= norm_len;

			 /* compute effective dielectric tensor: */

			 eps.m00 = (eps_inv_mean - eps_mean) * norm[0]*norm[0]
			           + eps_mean;
			 eps.m11 = (eps_inv_mean - eps_mean) * norm[1]*norm[1]
			           + eps_mean;
			 eps.m22 = (eps_inv_mean - eps_mean) * norm[2]*norm[2]
			           + eps_mean;
			 eps.m01 = (eps_inv_mean - eps_mean) * norm[0]*norm[1];
			 eps.m02 = (eps_inv_mean - eps_mean) * norm[0]*norm[2];
			 eps.m12 = (eps_inv_mean - eps_mean) * norm[1]*norm[2];

			 sym_matrix_invert(&md->eps_inv[ijk], eps);
		    }
		    else { /* undetermined normal vector and/or constant eps */
			 md->eps_inv[ijk].m00 = 1.0 / eps_mean;
			 md->eps_inv[ijk].m11 = 1.0 / eps_mean;
			 md->eps_inv[ijk].m22 = 1.0 / eps_mean;
			 md->eps_inv[ijk].m01 = 0.0;
			 md->eps_inv[ijk].m02 = 0.0;
			 md->eps_inv[ijk].m12 = 0.0;
		    }

		    eps_inv_total += md->eps_inv[ijk].m00
			           + md->eps_inv[ijk].m11
			           + md->eps_inv[ijk].m22;
	       }

     md->eps_inv_mean = eps_inv_total / (3 * md->local_N);
}
