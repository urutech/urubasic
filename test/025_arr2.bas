10 OPTION BASE 0
20 DIM A(5,3)
25 LET N = 0
30 FOR X=0 TO 5
40 FOR Y=0 TO 3
50 A(X,Y)=N
60 LET N=N+1
70 NEXT Y
80 NEXT X
90 FOR Y=0 TO 3
100 PRINT A(0, Y); A(1, Y); A(2, Y); A(3, Y); A(4,Y); A(5,Y)
110 NEXT Y
120 PRINT
200 LET B(1,1)=5
210 PRINT B(0,0); B(0,1)
220 PRINT B(1,0); B(1,1)
999 END