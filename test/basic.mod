(* Test case for declarations, assignments and operations involving variables
   and constants of basic type, excl. booleans. *)
MODULE basic;

  VAR
    i, j, k : INTEGER;
    b       : BYTE;
    r, s, t : SET;

  BEGIN
    (* Integer operations *)
    i := 42;
    j := 21;
    k := -i + j;
    k := -(j * i);
    k := j DIV i;
    k := j MOD i;
    k := 65536 + k;

    (* Set operations *)
    r := { 1, 29..31 };
    s := { 3..27 };
    t := r + s;
    t := t * s;
    t := (r + s) / s;
    t := (r + s) - s;

    (* Byte operations *)
    b := 21;
    b := b * 2

END basic.
