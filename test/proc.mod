MODULE proc;

  TYPE
    integer = RECORD ival : INTEGER END;

  VAR
    i : integer; j, k, l : INTEGER;

  (* Test passing records as arguments *)
  PROCEDURE IncAndGet(VAR x : integer) : INTEGER;
    BEGIN
      INC(x.ival)
    RETURN x.ival
  END IncAndGet;

  (* Test local procedures *)
  PROCEDURE AddFunc(x, y : INTEGER) : INTEGER;
    VAR
      z : INTEGER;

    PROCEDURE AddProc(VAR x : INTEGER; y : INTEGER);
      BEGIN
        x := x + y
      END AddProc;

    BEGIN
      AddProc(x, y); z := x
    RETURN z
  END AddFunc;

  (* Test passing strings as arguments to open array parameters *)
  PROCEDURE Len(x : ARRAY OF CHAR) : INTEGER;
    VAR
      i : INTEGER;
    BEGIN
      i := 0;
      WHILE ORD(x[i]) # 0 DO INC(i) END;
    RETURN i+1
  END Len;

  (* Test passing a single-character string as a char *)
  PROCEDURE Ord(ch : CHAR) : INTEGER;
    VAR
      i : INTEGER;
    BEGIN
      i := ORD(ch);
    RETURN i
  END Ord;

  BEGIN
    (* Test record parameters and function calls inside expressions *)
    i.ival := 1;
    i.ival := 1 + IncAndGet(i);   (* 3 *)

    j := AddFunc(i.ival, 2);      (* 5 *)
    k := Len("Hello, world!");    (* 14 *)
    l := Ord("H");                (* 72 *)

END proc.
