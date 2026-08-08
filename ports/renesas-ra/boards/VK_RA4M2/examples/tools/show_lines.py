f = open('/home/teodor/renesas_micropython/ports/renesas-ra/boards/VK_RA4M2/examples/r01uh0892ej0140-ra4m2.txt')
lines = f.readlines()
f.close()
for i in range(108530, 108610):
    print(i+1, lines[i].rstrip())
