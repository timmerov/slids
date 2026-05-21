/*
reopen classes.
*/

/* global scope class definition. */
GsA(int x_ = 0) {
    /* define things. */
    const int kA1 = 1;
    enum EnumA1 ( kEnumA1 );
    global int g_a1 = 2;

    void m_a1() {
        /* access scope things qualifier optional. */
        h = x_ + kA1 + kEnumA1 + g_a1;
        h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
        __println("GsA:m_a1");
    }

    /* hoisted class definition. */
    CsB(int y_ = 0) {
        /* define things. */
        const int kB1 = 6;
        enum EnumB1 ( kEnumB1 );
        global int g_b1 = 7;

        void m_b1() {
            /* access scope things qualifiers optional. */
            h = y_ + kB1 + kEnumB1 + g_b1;
            h = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            h = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            /* access enclosing scope things qualifier optional. */
            h = kA1 + kEnumA1 + g_a1;
            h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            __println("CsB:m_b1");
        }
    }
}

/* reopen global scope class. */
GsA() {
    /* define more things. */
    const int kA2 = 3;
    enum EnumA2 ( kEnumA2 );
    global int g_a2 = 4;

    void m_a2() {
        /* access scope things qualifier optional. */
        h = x_ + kA1 + kEnumA1 + g_a1 + kA1;
        h = kA2 + kEnumA2 + g_a2 + kA2;
        h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1 + GsA:kA1;
        h = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2 + GsA:kA2;
        __println("GsA:m_a2");
    }

    /* compile errors: re-definition. */
    //const int kA1 = 1;
    //enum EnumA1 ( kEnumA1 );
    //global int g_a1 = 2;
    //void m_a1() { }
}

/* compile error: re-definition. */
//GsA(int y_ = 5) { }

int32 main() {
    {
        GsA gsa;
        h = gsa.x_ + GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
        h = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
        gsa.m_a1();
        gsa.m_a2();

        GsA:CsB csb;
        h = csb.y_ + GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
        csb.m_b1();
    }

    /* compile errors: not in global scope. */
    //h = kA1;
    //h = kEnumA1;
    //h = g_a1;
    //h = kA2;
    //h = kEnumA2;
    //h = g_a2;
    //h = kB1;
    //h = kEnumB1;
    //h = g_b1;
    //h = CsB:kB1;
    //h = CsB:kEnumB1;
    //h = CsB:g_b1;

    return 0;
}
