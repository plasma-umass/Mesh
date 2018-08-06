iterations = 2000000

l = []

for it in range(1,4):
    for i in range( iterations ):
        l.append( None )

    for i in range( iterations ):
        l[i] = {}

    for i in range( iterations ):
        l[i] = None

    for i in range( iterations ):
        l[i] = {}
