MODULE strings;

  CONST
    greeting = "Hello, world!";
    char = "A";

  VAR
    str : ARRAY 14 OF CHAR;
    i : INTEGER;
    j : CHAR;

  BEGIN
    str := greeting;
    IF greeting # "Hello, shijie!" THEN i := 1 ELSE i := 0 END;
    j := char;

END strings.
