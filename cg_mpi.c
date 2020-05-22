/*

 * Sequential implementation of the Conjugate Gradient Method.

 *

 * Authors : Lilia Ziane Khodja & Charles Bouillaguet

 *

 * v1.02 (2020-04-3)

 *

 * CHANGE LOG:

 *    v1.01 : fix a minor printing bug in load_mm (incorrect CSR matrix size)

 *    v1.02 : use https instead of http in "PRO-TIP"

 *

 * USAGE:

 * 	$ ./cg --matrix bcsstk13.mtx                # loading matrix from file

 *      $ ./cg --matrix bcsstk13.mtx > /dev/null    # ignoring solution

 *	$ ./cg < bcsstk13.mtx > /dev/null           # loading matrix from stdin

 *      $  zcat matrix.mtx.gz | ./cg                # loading gziped matrix from

 *      $ ./cg --matrix bcsstk13.mtx --seed 42      # changing right-hand side

 *      $ ./cg --no-check < bcsstk13.mtx            # no safety check

 *

 * PRO-TIP :

 *      # downloading and uncompressing the matrix on the fly

 *	$ curl --silent https://hpc.fil.cool/matrix/bcsstk13.mtx.gz | zcat | ./cg

 */

#include <mpi.h>

#include <stdio.h>

#include <stdlib.h>

#include <err.h>

#include <math.h>

#include <getopt.h>

#include <sys/time.h>



#include "mmio.h"



#define THRESHOLD 1e-8		// maximum tolerance threshold

#define MASTER 0

struct csr_matrix_t {

	int n;			// dimension

	int nz;			// number of non-zero entries

	int *Ap;		// row pointers

	int *Aj;		// column indices

	double *Ax;		// actual coefficient

};



/*************************** Utility functions ********************************/



/* Seconds (wall-clock time) since an arbitrary point in the past */

double wtime()

{

	struct timeval ts;

	gettimeofday(&ts, NULL);

	return (double)ts.tv_sec + ts.tv_usec / 1e6;

}



/* Pseudo-random function to initialize b (rumors says it comes from the NSA) */

#define ROR(x, r) ((x >> r) | (x << (64 - r)))

#define ROL(x, r) ((x << r) | (x >> (64 - r)))

#define R(x, y, k) (x = ROR(x, 8), x += y, x ^= k, y = ROL(y, 3), y ^= x)

double PRF(int i, unsigned long long seed)

{

	unsigned long long y = i, x = 0xBaadCafe, b = 0xDeadBeef, a = seed;

	R(x, y, b);

	for (int i = 0; i < 31; i++) {

		R(a, b, i);

		R(x, y, b);

	}

	x += i;

	union { double d; unsigned long long l;	} res;

	res.l = ((x << 2) >> 2) | (((1 << 10) - 1ll) << 52);

	return 2 * (res.d - 1.5);

}



/*************************** Matrix IO ****************************************/



/* Load MatrixMarket sparse symetric matrix from the file descriptor f */

struct csr_matrix_t *load_mm(FILE * f)

{

	MM_typecode matcode;

	int n, m, nnz;



	/* -------- STEP 1 : load the matrix in COOrdinate format */

	double start = wtime();



	/* read the header, check format */

	if (mm_read_banner(f, &matcode) != 0)

		errx(1, "Could not process Matrix Market banner.\n");

	if (!mm_is_matrix(matcode) || !mm_is_sparse(matcode))

		errx(1, "Matrix Market type: [%s] not supported (only sparse matrices are OK)", mm_typecode_to_str(matcode));

	if (!mm_is_symmetric(matcode) || !mm_is_real(matcode))

		errx(1, "Matrix type [%s] not supported (only real symmetric are OK)", mm_typecode_to_str(matcode));

	if (mm_read_mtx_crd_size(f, &n, &m, &nnz) != 0)

		errx(1, "Cannot read matrix size");

	fprintf(stderr, "[IO] Loading [%s] %d x %d with %d nz in triplet format\n", mm_typecode_to_str(matcode), n, n, nnz);

	fprintf(stderr, "     ---> for this, I will allocate %.1f MByte\n", 1e-6 * (40.0 * nnz + 8.0 * n));



	/* Allocate memory for the COOrdinate representation of the matrix (lower-triangle only) */

	int *Ti = malloc(nnz * sizeof(*Ti));

	int *Tj = malloc(nnz * sizeof(*Tj));

	double *Tx = malloc(nnz * sizeof(*Tx));

	if (Ti == NULL || Tj == NULL || Tx == NULL)

		err(1, "Cannot allocate (triplet) sparse matrix");



	/* Parse and load actual entries */

	for (int u = 0; u < nnz; u++) {

		int i, j;

		double x;

		if (3 != fscanf(f, "%d %d %lg\n", &i, &j, &x))

			errx(1, "parse error entry %d\n", u);

		Ti[u] = i - 1;	/* MatrixMarket is 1-based */

		Tj[u] = j - 1;

		Tx[u] = x;

	}



	double stop = wtime();

	fprintf(stderr, "     ---> loaded in %.1fs\n", stop - start);



	/* -------- STEP 2: Convert to CSR (compressed sparse row) representation ----- */

	start = wtime();



	/* allocate CSR matrix */

	struct csr_matrix_t *A = malloc(sizeof(*A));

	if (A == NULL)

		err(1, "malloc failed");

	int *w = malloc((n + 1) * sizeof(*w));

	int *Ap = malloc((n + 1) * sizeof(*Ap));

	int *Aj = malloc(2 * nnz * sizeof(*Ap));

	double *Ax = malloc(2 * nnz * sizeof(*Ax));

	if (w == NULL || Ap == NULL || Aj == NULL || Ax == NULL)

		err(1, "Cannot allocate (CSR) sparse matrix");



	/* the following is essentially a bucket sort */



	/* Count the number of entries in each row */

	for (int i = 0; i < n; i++)

		w[i] = 0;

	for (int u = 0; u < nnz; u++) {

		int i = Ti[u];

		int j = Tj[u];

		w[i]++;

		if (i != j)	/* the file contains only the lower triangular part */

			w[j]++;

	}



	/* Compute row pointers (prefix-sum) */

	int sum = 0;

	for (int i = 0; i < n; i++) {

		Ap[i] = sum;

		sum += w[i];

		w[i] = Ap[i];

	}

	Ap[n] = sum;



	/* Dispatch entries in the right rows */

	for (int u = 0; u < nnz; u++) {

		int i = Ti[u];

		int j = Tj[u];

		double x = Tx[u];

		Aj[w[i]] = j;

		Ax[w[i]] = x;

		w[i]++;

		if (i != j) {	/* off-diagonal entries are duplicated */

			Aj[w[j]] = i;

			Ax[w[j]] = x;

			w[j]++;

		}

	}



	/* release COOrdinate representation */

	free(w);

	free(Ti);

	free(Tj);

	free(Tx);

	stop = wtime();

	fprintf(stderr, "     ---> converted to CSR format in %.1fs\n", stop - start);

	fprintf(stderr, "     ---> CSR matrix size = %.1fMbyte\n", 1e-6 * (24. * nnz + 4. * n));



	A->n = n;

	A->nz = sum;

	A->Ap = Ap;

	A->Aj = Aj;

	A->Ax = Ax;

	return A;

}



/*************************** Répartition matrices et vecteurs entre les processus *********************************/

/*Répartition des vecteurs */

void scattVector(double *b,double *b_local,int nrow,int n_local,int my_rank, int np)
{
	int n_loc, Ap_count[np], Ap_moves[np];

	if(my_rank==MASTER){

		int n_loc_der; // dernier processus

		n_loc = ceil((double)nrow/ np); // taille du vecteur de chaque processus

		n_loc_der = nrow - (np-1)*n_loc;    //taille du dernier vecteur p .



		if (n_loc_der < 1 || n_loc_der==nrow) { //Error case

			printf("Nombre de processeurs imcompatibles ...\n");

			exit(0);

		}

		// Obtenir la taille et le déplacement de chque processus

		for (int p = 0; p < np-1; p++) {

			Ap_count[p] = n_loc;

			Ap_moves[p] = p*n_loc;

		}

		Ap_count[np-1] = n_loc_der;

		Ap_moves[np-1] =(np-1)*n_loc;

	}

	MPI_Scatterv(b,Ap_count,Ap_moves,MPI_DOUBLE,b_local,n_local,MPI_DOUBLE,MASTER,MPI_COMM_WORLD);

}

/*Répartition de la matrice*/

void scattMatrix(const struct csr_matrix_t *A,struct csr_matrix_t *A_local,int *nrow,int my_rank,int np){

	int nz_local,n_local;

	int *Aj_local,*Ap_local;

	double *Ax_local;

	int Ap_count[np], Ap_moves[np];



	if(my_rank==MASTER){

		int n_local_der;

		*nrow=A->n;

		n_local = ceil((double)*nrow/ np);

		n_local_der = *nrow - (np-1)*n_local;



		if (n_local_der < 1 || n_local_der==*nrow) { //Error case

			printf("Nombre de processeurs imcompatibles ...\n");

			exit(0);

		}

		// Même procédé que pour les vecteurs

		for (int p = 0; p < np-1; p++) {

			Ap_count[p] = n_local;

			Ap_moves[p] = p*n_local;

		}

		Ap_count[np-1] = n_local_der;

		Ap_moves[np-1] =(np-1)*n_local;

		int Ax_count[np];

		int Ax_moves[np];

		for (int p = 0; p < np; p++) {

			Ax_count[p] = A->Ap[p*n_local+Ap_count[p]]-A->Ap[p*n_local];

			Ax_moves[p] = A->Ap[Ap_moves[p]];

		}

		//Le maitre envoye l'ensemble des lignes aux esclaves

		MPI_Bcast(nrow,1,MPI_INT,MASTER,MPI_COMM_WORLD);

		//Le maitre envoie nz à tous les processus esclaves

		MPI_Scatter(Ap_count,1,MPI_INT,MPI_IN_PLACE,1,MPI_INT,MASTER,MPI_COMM_WORLD);

		// envoie Ap

		MPI_Scatterv(A->Ap,Ap_count,Ap_moves,MPI_INT,MPI_IN_PLACE,0,MPI_INT,MASTER,MPI_COMM_WORLD);

		//envoie Ax

		MPI_Scatterv(A->Ax,Ax_count,Ax_moves,MPI_DOUBLE,MPI_IN_PLACE,0,MPI_DOUBLE,MASTER,MPI_COMM_WORLD);

		//envoie Aj

		MPI_Scatterv(A->Aj,Ax_count,Ax_moves,MPI_INT,MPI_IN_PLACE,0,MPI_INT,MASTER,MPI_COMM_WORLD);

		nz_local=Ax_count[0];

		Ap_local= (int *)malloc(sizeof(int) *(n_local+1));

		Ax_local = (double *)malloc(sizeof(double) * nz_local);

		Aj_local = (int *)malloc(sizeof(int) * nz_local);

		for(int i=0;i<n_local;i++){

			Ap_local[i]=A->Ap[i];

		}

		Ap_local[n_local]=nz_local;

		for(int i=0;i<nz_local;i++){

			Ax_local[i]=A->Ax[i];

			Aj_local[i]=A->Aj[i];

		}

	}else{

		//recevoir les lignes depuis le maitre

		MPI_Bcast(nrow,1,MPI_INT,MASTER,MPI_COMM_WORLD);

		n_local = ceil((double)*nrow/ np);

		if(my_rank==np-1){n_local=*nrow - (np-1)*n_local;}

		//recevoir nz

		MPI_Scatter(MPI_IN_PLACE,1,MPI_INT,&nz_local,1,MPI_INT,MASTER,MPI_COMM_WORLD);

		//recevoir Ap

		Ap_local= (int *)malloc(sizeof(int) *(n_local+1));

		MPI_Scatterv(MPI_IN_PLACE,0,MPI_IN_PLACE,MPI_INT,Ap_local,n_local,MPI_INT,MASTER,MPI_COMM_WORLD);

		Ap_local[n_local]=nz_local;

		//recevoir Ax

		Ax_local = (double *)malloc(sizeof(double) * nz_local);

		MPI_Scatterv(MPI_IN_PLACE,0,MPI_IN_PLACE,MPI_DOUBLE,Ax_local,nz_local,MPI_DOUBLE,MASTER,MPI_COMM_WORLD);

		//recevoir Aj

		Aj_local = (int *)malloc(sizeof(int) * nz_local);

		MPI_Scatterv(MPI_IN_PLACE,0,MPI_IN_PLACE,MPI_INT,Aj_local,nz_local,MPI_INT,MASTER,MPI_COMM_WORLD);

	}



	//construction de la matrice pour chaque processus

	A_local->n = n_local;

	A_local->nz = nz_local;

	A_local->Ap = Ap_local;

	A_local->Aj = Aj_local;

	A_local->Ax = Ax_local;



}



/*************************** Matrix accessors *********************************/

/* Copy the diagonal of A into the vector d. */

void extract_diagonal(const struct csr_matrix_t *A, double *d,int my_rank)

{

	int n = A->n;

	int *Ap = A->Ap;

	int *Aj = A->Aj;

	double *Ax = A->Ax;

	if(Ap[0]!=0){

		int diff,tmp=0;

		int prec=Ap[0]; //précédent

		Ap[0]=0;

		diff=Ap[1]-prec; //différence de distance entre processus

		tmp=tmp+diff;

		prec=Ap[1];

		Ap[1]=tmp;

		for (int i = 0; i < n; i++) {

			d[i] = 0.0;

			if(i>0 && i+1<n){

				diff=Ap[i+1]-prec;

				tmp=tmp+diff;

				prec=Ap[i+1];

				Ap[i+1]=tmp;

			}

			for (int u = Ap[i]; u < Ap[i + 1]; u++)

				if (my_rank*n+i ==  Aj[u])

					d[i] += Ax[u];

		}

	}else{

		for (int i = 0; i < n; i++) {

			d[i] = 0.0;

			for (int u = Ap[i]; u < Ap[i + 1]; u++)

				if (i == Aj[u])

					d[i] += Ax[u];

		}

	}

}

/* Matrix-vector product (with A in CSR format) : y = values */

void sp_gemv(const struct csr_matrix_t *A, const double *x, double *y)

{

	int n = A->n;

	int *Ap = A->Ap;

	int *Aj = A->Aj;

	double *Ax = A->Ax;

	if(Ap[0]!=0){

		int diff,tmp=0;

		int prec=Ap[0];

		Ap[0]=0;

		diff=Ap[1]-prec;

		tmp=tmp+diff;

		prec=Ap[1];

		Ap[1]=tmp;

		for (int i = 0; i < n; i++) {

			y[i] = 0;

			if(i>0 && i+1<n){

				diff=Ap[i+1]-prec;

				tmp=tmp+diff;

				prec=Ap[i+1];

				Ap[i+1]=tmp;

			}

			for (int u = Ap[i]; u < Ap[i + 1]; u++) {

				int j = Aj[u];

				double A_ij = Ax[u];

				y[i] += A_ij * x[j];

			}

		}

	}else{

		for (int i = 0; i < n; i++) {

			y[i] = 0;

			for (int u = Ap[i]; u < Ap[i + 1]; u++) {

				int j = Aj[u];

				double A_ij = Ax[u];

				y[i] += A_ij * x[j];

			}

		}

	}

}



/*************************** Vector operations ********************************/

/* dot product */

double dot(const int n, const double *x, const double *y)

{
	double sum = 0.0;
	for (int i = 0; i < n; i++)
		sum += x[i] * y[i]; //calcul de la somme locale pour chaque processeurs
  return sum;
}





/* euclidean norm (a.k.a 2-norm) */

double norm(const int n, const double *x)

{
	return sqrt(dot(n, x, x));
}



/*********************** conjugate gradient algorithm *************************/



/* Solve values == b (the solution is written in x). Scratch must be preallocated of size 6n */

void cg_solve(const struct csr_matrix_t *A_local, const double *b_local, double *x, const double epsilon,int nrow,int my_rank)

{

	int n_local = A_local->n;

	int nz_local = A_local->nz;

  if(my_rank==MASTER){

		fprintf(stderr, "[CG] Starting iterative solver\n");

		fprintf(stderr, "     ---> Working set : %.1fMbyte\n", 1e-6 * (12.0 * nz_local + 52.0 * n_local));

		fprintf(stderr, "     ---> Per iteration: %.2g FLOP in sp_gemv() and %.2g FLOP in the rest\n_local", 2. * nz_local, 12. * n_local);

	}

	double *r_local = malloc(n_local * sizeof(double));	        // residue

	double *z_local =malloc(n_local * sizeof(double));// scratch + n_local;	// preconditioned-residue

	double *p_local =malloc(n_local * sizeof(double));// scratch + 2 * n_local;	// search direction

	double *q = malloc(n_local * sizeof(double));//scratch + 3 * n_local;	// q == Ap

	double *d_local = malloc(n_local * sizeof(double));	// diagonal entries of A_local (Jacobi preconditioning)

	double *final_p = malloc(nrow * sizeof(double));//final vector

	double *x_local = malloc(n_local * sizeof(double));//local solution





	/* Isolate diagonal */

	extract_diagonal(A_local, d_local,my_rank);





	/*

	 * This function follows closely the pseudo-code given in the (english)

	 * Wikipedia page "Conjugate gradient method". This is the version with

	 * preconditionning.

	 */



	/* We use x_local == 0 --- évite le premier prodduit matrix-vector. */

	for (int i = 0; i < n_local; i++)

		x_local[i] = 0.0;

	for (int i = 0; i < n_local; i++)	// r_local <-- b_local - values == b_local

		r_local[i] = b_local[i];

	for (int i = 0; i < n_local; i++)	// z_local <-- M^(-1).r_local

		z_local[i] = r_local[i] / d_local[i];

	for (int i = 0; i < n_local; i++)	// p_local <-- z_local

		p_local[i] = z_local[i];



	double rz = dot(n_local, r_local, z_local);



	double start = wtime();

	double last_display = start;

	int iter = 0;

	while (norm(n_local, r_local) > epsilon) {

		/* loop invariant : rz = dot(r_local, z_local) */

		double old_rz = rz;

		MPI_Allgather(p_local, n_local, MPI_DOUBLE, final_p, n_local, MPI_DOUBLE, MPI_COMM_WORLD);

		sp_gemv(A_local, final_p, q);	/* q <-- A_local.p_local */

		double alpha = old_rz / dot(n_local, p_local, q);

		for (int i = 0; i < n_local; i++)	// x_local <-- x_local + alpha*p_local

			x_local[i] += alpha * p_local[i];

		for (int i = 0; i < n_local; i++)	// r_local <-- r_local - alpha*q

			r_local[i] -= alpha * q[i];

		for (int i = 0; i < n_local; i++)	// z_local <-- M^(-1).r_local

			z_local[i] = r_local[i] / d_local[i];

		rz = dot(n_local, r_local, z_local);	// restore invariant

		double beta = rz / old_rz;

		for (int i = 0; i < n_local; i++)	// p_local <-- z_local + beta*p_local   /* p_local = r_local + beta.p_local */



			p_local[i] = z_local[i] + beta * p_local[i];

		iter++;

		if(my_rank==MASTER){

			double t = wtime();

			if (t - last_display > 0.5) {

				/* verbosity */

				double rate = iter / (t - start);	// iterations per s.

				double GFLOPs = 1e-9 * rate * (2 * nz_local + 12 * n_local);

				fprintf(stderr, "\r    MASTER-->iter : %d (%.1f it/s, %.2f GFLOPs)",iter, rate, GFLOPs);

				fflush(stdout);

				last_display = t;

			}

		}

	}

  /*on récupère la solution*/

	MPI_Gather(x_local,n_local,MPI_DOUBLE,x,n_local,MPI_DOUBLE,MASTER,MPI_COMM_WORLD);

	if(my_rank==MASTER){

		fprintf(stderr, "\n   ----> Finished in %.1fs and %d_local iterations\n", wtime() - start, iter);

	}

}



/******************************* main program *********************************/



/* options descriptor */

struct option longopts[6] = {

	{"seed", required_argument, NULL, 's'},

	{"rhs", required_argument, NULL, 'r'},

	{"matrix", required_argument, NULL, 'm'},

	{"solution", required_argument, NULL, 'o'},

	{"no-check", no_argument, NULL, 'c'},

	{NULL, 0, NULL, 0}

};



int main(int argc, char **argv)

{

	int np,     /* nombre de processus*/

        my_rank;       /* id processus */

				/* Initialisation MPI */

	MPI_Init(&argc, &argv);

	MPI_Comm_size(MPI_COMM_WORLD, &np);

	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

	struct csr_matrix_t *A=NULL,*A_local=NULL;

	double *b_local/*right-hand side local*/,

	 *x={0}	/* solution vector */ ,*b={0},	/* right-hand side */

	*scratch=NULL;

	/* Parse command-line options */

	int safety_check=0,n=0;

	char *solution_filename;

	if(my_rank==MASTER){

		long long seed = 0;

		char *rhs_filename = NULL;

		char *matrix_filename = NULL;

		solution_filename = NULL;

		safety_check = 1;

		char ch;

		while ((ch = getopt_long(argc, argv, "", longopts, NULL)) != -1) {

			switch (ch) {

			case 's':

				seed = atoll(optarg);

				break;

			case 'r':

				rhs_filename = optarg;

				break;

			case 'm':

				matrix_filename = optarg;

				break;

			case 'o':

				solution_filename = optarg;

				break;

			case 'c':

				safety_check = 0;

				break;

			default:

				errx(1, "Unknown option");

			}

		}



		/* Load the matrix */

		FILE *f_mat = stdin;

		if (matrix_filename) {

			f_mat = fopen(matrix_filename, "r");

			if (f_mat == NULL)

				err(1, "cannot matrix file %s", matrix_filename);

		}

		A = load_mm(f_mat);



		/* Allocate memory */

		n = A->n;

		double *mem = malloc(7 * n * sizeof(double));

		if (mem == NULL)

			err(1, "cannot allocate dense vectors");

		x = mem;	/* solution vector */

		b = mem + n;	/* right-hand side */

		scratch = mem + 2 * n;	/* workspace for cg_solve() */



		/* Prepare right-hand size */

		if (rhs_filename) {	/* load from file */

			FILE *f_b = fopen(rhs_filename, "r");

			if (f_b == NULL)

				err(1, "cannot open %s", rhs_filename);

			fprintf(stderr, "[IO] Loading b from %s\n", rhs_filename);

			for (int i = 0; i < n; i++) {

				if (1 != fscanf(f_b, "%lg\n", &b[i]))

					errx(1, "parse error entry %d\n", i);

			}

			fclose(f_b);

		} else {

			for (int i = 0; i < n; i++)

				b[i] = PRF(i, seed);

		}



}



	/* Allocationde la mémoire pour chaque processeur */

	A_local = malloc(sizeof(*A_local));



  /*répartition de la matrice entre les processeurs*/

	scattMatrix(A,A_local,&n,my_rank,np);

	b_local=(double *)malloc(sizeof(double) * A_local->n);





	/*répartition du vecteur b entre les processeurs*/

	scattVector(b,b_local,n,A_local->n,my_rank,np);



	/* solve values Ax=b */

	cg_solve(A_local,b_local,x,THRESHOLD,n,my_rank);

	if(my_rank==MASTER){

		printf("SOLUTION: x[0]=%f, x[F]=%f\n",x[0],x[n-1]);

	}

	if(my_rank==MASTER)
	{

	/* Check result */ 

	if (safety_check) {

		double *y = scratch;

		sp_gemv(A, x, y);	// y = Ax

		for (int i = 0; i < n; i++)	// y = Ax - b

			y[i] -= b[i];

		//fprintf(stderr, "[check] max error = %2.2e\n", norm(n, y));

	}



	/* Dump the solution vector */

	FILE *f_x = stdout;

	if (solution_filename != NULL) {

		f_x = fopen(solution_filename, "w");

		if (f_x == NULL)

			err(1, "cannot open solution file %s", solution_filename);

		fprintf(stderr, "[IO] writing solution to %s\n", solution_filename);

	}

	for (int i = 0; i < n; i++)

		fprintf(f_x, "%a\n", x[i]);

	}


	
	MPI_Finalize();

	return EXIT_SUCCESS;

}
