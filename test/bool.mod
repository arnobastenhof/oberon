(* Test case for boolean expressions. *)
MODULE boolean;

  VAR
    p, q, r : BOOLEAN;

  BEGIN
    p := TRUE;
    q := ~TRUE;
    r := (p & q) OR (p & ~q) OR (~p & q) OR (~p & ~q);
    r := (p OR q) & (p OR ~q) & (~p OR q) & (~p OR ~q);
    r := p OR (q & (p OR q))

END boolean.
