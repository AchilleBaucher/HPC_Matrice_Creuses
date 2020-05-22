# Check si la matrice dans soluce et dans vraie_soluce sont les mêmes
with open('soluce.mtx','r') as s :
    with open('vraie_soluce.mtx','r') as sv :
        print(s.read() == sv.read())

