10 A = 130000 : GOSUB 1000
30 PRINT "Square root of";A;"is";X; "(";X;"*";X;"=";X*X;")"
40 IF X <> 360 THEN PRINT "FAILED" : END
50 PRINT "SUCC"; : PRINT "ESS"

999 END



1000 REM calc square root of A. result in X
1010 X=0 : y=1 : z=1
1040 IF y > A THEN RETURN
1050 X=X+1
1060 z=z+2
1070 y=y+z
1080 GOTO 1040
