# HPC_Matrice_Creuses
Projet scolaire de HPC pour résoudre un système avec matrice creuse.

# Info utiles
* Site du projet : https://cbouilla.github.io//HPC.html
* Les matrices son téléchargeables ici : http://hpc.fil.cool/matrix/
* Overleaf : https://www.overleaf.com/4426958872xhrnxcxjspvb

# Compiler & exécuter
make

./cg --matrix *fichier_matrice.mtx* --solution *fichier_solution.mtx*

avec *fichier_matrice.mtx* la matrice choisie mtx (ex : bcsstk13.mtx)
et *fichier_solution.mtx* le fichier dans lequel sera écrite la solution.

# Ssh
https://cbouilla.github.io//HPC.html
ssh NUMETU@ssh.ufr-info-p6.jussieu.fr
ssh NUMETU@ppti-14-305-01

# ERREURS
 1. Json
SERVER ERROR : Tentative de fraude grossière à la mise en gage !
Aborting.
*-> Supprimer le json de la matrice, relancer*

2. Nan
SERVER ERROR : Le coefficient x[**88**] dans la mise en gage n'est pas le même que celui dans coeffs. Dans response : nan VS dans coeffs : nan
-> Je ne sais pas du tout
