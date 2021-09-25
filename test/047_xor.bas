20 PRINT " X"," Y"," X XOR Y"," X = Y"
30 FOR X= 0 TO 1
40 FOR Y= 0 TO 1
40 Z = (X OR Y)-(X AND Y) : REM calculates XOR(X, Y)
60 PRINT X,Y,Z,1-Z
70 NEXT Y
80 NEXT X
