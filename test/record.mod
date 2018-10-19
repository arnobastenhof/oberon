MODULE record;

  TYPE
    person*  = RECORD age : INTEGER END;
    employee = RECORD (person) employeeId : INTEGER END;
    retiree  = RECORD (person) pensionId: INTEGER END;

  VAR
    p : person;
    e : employee;
    r : retiree;

  BEGIN
    p.age := 42;
    e.age := 21;
    e.employeeId := 1;
    r.age := 67;
    r.pensionId := 2;

END record.
