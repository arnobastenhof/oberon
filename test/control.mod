MODULE control;

  VAR
    i, j : INTEGER;

  BEGIN
    IF 1 IN {1, 3..5} THEN j := 1 END;    (* j = 1 *)
    FOR i := 1 TO 8 BY 1 DO INC(j) END;   (* i = 8, j = 9 *)
    WHILE i # 0 DO INC(j); DEC(i) END;    (* i = 0, j = 17 *)
    REPEAT INC(i); INC(j) UNTIL i = 8     (* i = 8, j = 25 *)

END control.
