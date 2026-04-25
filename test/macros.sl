/*
test the stringification operator.
*/

int32 main() {
    int some_int = 42;
    float64 some_float = 3.14;
    bool some_bool = true;

    // ##name(x) — variable name as string
    char[] n1 = ##name(some_int);      // "some_int"
    char[] n2 = ##name(some_float);    // "some_float"
    char[] n3 = ##name(some_bool);     // "some_bool"
    __println("##name(some_int  )=" + n1);
    __println("##name(some_float)=" + n2);
    __println("##name(some_bool )=" + n3);

    // ##type(x) — variable type as string
    char[] t1 = ##type(some_int);       // "int"
    char[] t2 = ##type(some_float);    // "float64"
    char[] t3 = ##type(some_bool);     // "bool"
    __println("##type(some_int  )=" + t1);
    __println("##type(some_float)=" + t2);
    __println("##type(some_bool )=" + t3);

    // ##line — current source line as string
    char[] ln = ##line;                // line number where this appears
    __println("##line=" + ln);

    // ##file — source filename as string
    char[] f = ##file;                 // "macros.sl"
    __println("##file=" + f);

    // ##func — enclosing function name as string
    char[] fn = ##func;                // "test"
    __println("##func=" + fn);

    // ##date — compilation date as string (e.g. "Apr 25 2026")
    char[] d = ##date;
    __println("##date=" + d);

    // ##time — compilation time as string (e.g. "12:34:56")
    char[] tm = ##time;
    __println("##time=" + tm);

    // #x — desugar: (##type(x), ##name(x), x)
    tpi = #some_int;     // ("int", "some_int", 42)
    tpf = #some_float;   // ("float64", "some_float", 3.14)
    tpb = #some_bool;    // ("bool", "some_bool", true)
    __println("#some_int  : " + tpi[0] + " " + tpi[1] + " = " + tpi[2]);
    __println("#some_float: " + tpf[0] + " " + tpf[1] + " = " + tpf[2]);
    __println("#some_bool : " + tpb[0] + " " + tpb[1] + " = " + tpb[2]);

    return 0;
}
