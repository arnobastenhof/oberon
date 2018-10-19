MODULE pointer;

  TYPE
    personPtr = POINTER TO person;
    person = RECORD age : INTEGER END;

  VAR
    p : person;
    ptr : personPtr;

  BEGIN
    p.age := 42;
    ptr := SYSTEM.VAL(personPtr, SYSTEM.ADR(p));
    ptr^.age := 21

END pointer.
