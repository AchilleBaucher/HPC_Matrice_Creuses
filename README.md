# HPC_Matrice_Creuses
Projet scolaire de HPC pour résoudre un système avec matrice creuse.

# Info utiles
* Site du projet : https://cbouilla.github.io//HPC.html
* Les matrices son téléchargeables ici : http://hpc.fil.cool/matrix/
* Overleaf : https://www.overleaf.com/4426958872xhrnxcxjspvb

# Avant de commencer
## Ssh
ssh NUMETU@ssh.ufr-info-p6.jussieu.fr
ssh NUMETU@ppti-14-305-01

## Internet
export http_proxy="proxy.ufr-info-p6.jussieu.fr:3128"
export https_proxy="proxy.ufr-info-p6.jussieu.fr:3128"

# Compiler & exécuter
make

python3 runner.py

./cg --matrix *fichier_matrice.mtx* --solution *fichier_solution.mtx*

avec *fichier_matrice.mtx* la matrice choisie mtx (ex : bcsstk13.mtx)
et *fichier_solution.mtx* le fichier dans lequel sera écrite la solution.

## Achille
ssh 3672146@ssh.ufr-info-p6.jussieu.fr
ssh 3672146@ppti-14-305-01

./cg --matrix bcsstk13.mtx --seed 136467246365411317

mpiexec --n 4 -hostfile nodes.txt --map-by ppr:1:node ./cg --matrix bcsstk13.mtx --seed 136467246365411317

# ERREURS
 1. Json
SERVER ERROR : Tentative de fraude grossière à la mise en gage !
Aborting.
*-> Supprimer le json de la matrice, relancer*

2. Nan
SERVER ERROR : Le coefficient x[**88**] dans la mise en gage n'est pas le même que celui dans coeffs. Dans response : nan VS dans coeffs : nan
-> Je ne sais pas du tout
