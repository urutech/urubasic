
100 LET Y = -42
110 IF Y <> -42 THEN  900
120 LET Y = Y * -1
130 IF Y <> 42 THEN  900
140 LET Y = +1 + Y + +1 + -2
150 IF Y <> 42 THEN  900
160 REM comment comment comment comment
800 PRINT "SUCCESS"
810 STOP
900 PRINT "FAILED"
910 END