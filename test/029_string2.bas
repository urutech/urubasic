10 A$="SATURDAY MORNING"
20 FOR X=0 TO LEN(A$)+1
30 PRINT LEFT$(A$, X)
40 NEXT X
50 FOR X=0 TO 17
60 PRINT RIGHT$("SUNDAY MORNING", X)
70 NEXT X

80 A$="MONDAY"+" "+ "MORNING"
90 B$=MID$(A$,4,3)
100 PRINT B$
110 B$=MID$(A$,1,6)
120 PRINT B$
130 B$=MID$(A$,8)
140 PRINT B$

999 END
