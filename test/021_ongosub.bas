10 LET X = 2
20 ON X GOSUB 100, 200, 300
30 IF X=42 THEN PRINT "SUCCESS"
40 END
100 PRINT "FAILED"
110 RETURN
200 LET X=42
210 RETURN
300 GOTO 100
999 END

