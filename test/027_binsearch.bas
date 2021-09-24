10 REM BINARY SEARCH
20 OPTION BASE 0
30 DIM A(99)
40 DATA 8,37,29,28,23,22,13,7,1
50 GOSUB 300
60 GOSUB 400
70 REM PRINT "WHAT VALUE DO YOU SEEK";
80 REM INPUT X
85 LET X = 13
90 GOSUB 500
100 IF F=0 THEN 130
110 PRINT A(M);"FOUND AT INDEX";M
120 GOTO 140
130 PRINT X;"NOT FOUND"
140 STOP
300 REM LOAD ARRAY
310 READ N
320 FOR I=0 TO N-1
330 READ A(I)
340 NEXT I
350 RETURN
400 REM BUBBLE SORT
410 FOR I=N-1 TO 1 STEP -1
420 FOR J=0 TO I-1
430 IF A(J)<=A(J+1) THEN 470
440 LET T=A(J)
450 LET A(J)=A(J+1)
460 LET A(J+1)=T
470 NEXT J
480 NEXT I
490 RETURN
500 REM BINARY SEARCH
510 REM N IS NUMBER OF ELEMENTS
520 REM A IS ARRAY OF ELEMENTS
530 REM X IS VALUE WE SEEK
540 REM F IS FLAG, 0 IS NOT FOUND, 1 IS FOUND
550 REM L IS LOWEST INDEX
560 REM H IS HIGHEST INDEX
570 REM M IS MIDDLE INDEX
580 LET L=0
590 LET H=N-1
600 LET F=0
610 IF H<L THEN 730
620 LET M=L+INT((H-L)/2)
630 IF A(M)=X THEN 710
640 IF A(M)>X THEN 680
650 REM A(M) IS TOO SMALL
660 LET L=M+1
670 GOTO 610
680 REM A(M) IS TOO BIG
690 LET H=M-1
700 GOTO 610
710 REM FOUND IT
720 LET F=1
730 RETURN
740 END
