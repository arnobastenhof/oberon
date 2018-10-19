MODULE array;

  TYPE
    vec = ARRAY 4 OF INTEGER;
    mat = ARRAY 2+2, 2*2 OF INTEGER;

  VAR
    i : INTEGER;
    v : vec;
    m : mat;

  BEGIN 
    FOR i := 0 TO 3 DO v[i] := i + 1 END;
    FOR i := 0 TO 3 DO m[i] := v END;
    m[1,2] := m[2][1]

END array.
