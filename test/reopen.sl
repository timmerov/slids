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
    CsB(int y_ = 17) {
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
            /* access sibling scope things with qualifiers. */
            h = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            h = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            __println("CsB:m_b1");
        }
    }

    /* derived class definition from global class. */
    GsC : CsD(int x_ = 14) {
        /* define things. */
        const int kD1 = 15;
        enum EnumD1 ( kEnumD1 );
        global int g_d1 = 16;

        void m_d1() {
            /* access scope things qualifiers optional. */
            h = x_ + kD1 + kEnumD1 + g_d1;
            h = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            h = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            /* access enclosing scope things qualifier optional. */
            h = kA1 + kEnumA1 + g_a1;
            h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            /* access sibling scope things with qualifiers. */
            h = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            h = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            __println("CsD:m_d1");
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

    /* reopen hoisted class. */
    CsB() {
        /* define more things. */
        const int kB2 = 8;
        enum EnumB2 ( kEnumB2 );
        global int g_b2 = 9;

        void m_b2() {
            /* access scope things qualifiers optional. */
            h = y_ + kB1 + kEnumB1 + g_b1;
            h = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            h = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            h = kB2 + kEnumB2 + g_b2;
            h = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
            h = GsA:CsB:kB2 + GsA:CsB:kEnumB2 + GsA:CsB:g_b2;
            /* access enclosing scope things qualifier optional. */
            h = kA1 + kEnumA1 + g_a1;
            h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            h = kA2 + kEnumA2 + g_a2;
            h = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
            /* access sibling scope things with qualifiers. */
            h = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            h = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            h = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
            h = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
            __println("CsB:m_b2");
        }
    }

    /* in-class inline reopen. */
    void CsB:m_b3() {
        /* access scope things qualifiers optional. */
        h = y_ + kB1 + kEnumB1 + g_b1;
        h = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
        h = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
        h = kB2 + kEnumB2 + g_b2;
        h = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
        h = GsA:CsB:kB2 + GsA:CsB:kEnumB2 + GsA:CsB:g_b2;
        /* access enclosing scope things qualifier optional. */
        h = kA1 + kEnumA1 + g_a1;
        h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
        h = kA2 + kEnumA2 + g_a2;
        h = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
        __println("CsB:m_b3");
    }

    /* reopen derived class. */
    CsD() {
        /* define more things. */
        const int kD2 = 19;
        enum EnumD2 ( kEnumD2 );
        global int g_d2 = 20;

        void m_d2() {
            /* access scope things qualifiers optional. */
            h = x_ + kD1 + kEnumD1 + g_d1;
            h = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            h = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            h = kD2 + kEnumD2 + g_d2;
            h = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
            h = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
            /* access enclosing scope things qualifier optional. */
            h = kA1 + kEnumA1 + g_a1;
            h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            h = kA2 + kEnumA2 + g_a2;
            h = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
            /* access sibling scope things with qualifiers. */
            h = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            h = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            h = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
            h = GsA:CsB:kB2 + GsA:CsB:kEnumB2 + GsA:CsB:g_b2;
            __println("CsD:m_d2");
        }
    }

    /* in-class inline reopen */
    void CsD:m_d3() {
        /* access scope things qualifiers optional. */
        h = x_ + kD1 + kEnumD1 + g_d1;
        h = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
        h = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
        h = kD2 + kEnumD2 + g_d2;
        h = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
        h = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
        /* access enclosing scope things qualifier optional. */
        h = kA1 + kEnumA1 + g_a1;
        h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
        h = kA2 + kEnumA2 + g_a2;
        h = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
        /* access sibling scope things with qualifiers. */
        h = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
        h = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
        h = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
        h = GsA:CsB:kB2 + GsA:CsB:kEnumB2 + GsA:CsB:g_b2;
        __println("CsD:m_d3");
    }

    /* compile error: re-definition. */
    //CsB(int y_ = 10) { }
    //CsD(int y_ = 21) { }
}

/* inline reopen. */
void GsA:m_a3() {
    /* access scope things qualifier optional. */
    h = x_ + kA1 + kEnumA1 + g_a1 + kA1;
    h = kA2 + kEnumA2 + g_a2 + kA2;
    h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1 + GsA:kA1;
    h = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2 + GsA:kA2;
    __println("GsA:m_a3");
}

/* inline reopen hoisted class. */
void GsA:CsB:m_b4() {
    /* access scope things qualifiers optional. */
    h = y_ + kB1 + kEnumB1 + g_b1;
    h = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
    h = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
    h = kB2 + kEnumB2 + g_b2;
    h = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
    h = GsA:CsB:kB2 + GsA:CsB:kEnumB2 + GsA:CsB:g_b2;
    /* access enclosing scope things qualifier optional. */
    h = kA1 + kEnumA1 + g_a1;
    h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
    h = kA2 + kEnumA2 + g_a2;
    h = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
    __println("CsB:m_b4");
}

/* inline reopen derived class */
void CsD:m_d4() {
    /* access scope things qualifiers optional. */
    h = x_ + kD1 + kEnumD1 + g_d1;
    h = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
    h = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
    h = kD2 + kEnumD2 + g_d2;
    h = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
    h = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
    /* access enclosing scope things qualifier optional. */
    h = kA1 + kEnumA1 + g_a1;
    h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
    h = kA2 + kEnumA2 + g_a2;
    h = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
    /* access sibling scope things with qualifiers. */
    h = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
    h = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
    h = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
    h = GsA:CsB:kB2 + GsA:CsB:kEnumB2 + GsA:CsB:g_b2;
    __println("CsD:m_d4");
}

/* global scope class definition. */
GsC(int z_ = 11) {
    /* define things. */
    const int kC1 = 12;
    enum EnumC1 ( kEnumC1 );
    global int g_c1 = 13;

    void m_c1() {
        __println("GsC:m_c1");
    }
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
        gsa.m_a3();

        GsA:CsB csb;
        h = csb.y_ + GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
        csb.m_b1();
        csb.m_b2();
        csb.m_b3();
        csb.m_b4();

        GsA:CsD csd;
        h = csd.x_ + GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
        csd.m_d1();
        csd.m_d2();
        csd.m_d3();
        csd.m_d4();
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
    //h = kB2;
    //h = kEnumB2;
    //h = g_b2;
    //h = CsB:kB2;
    //h = CsB:kEnumB2;
    //h = CsB:g_b2;
    //h = kD1;
    //h = kEnumD1;
    //h = g_d1;
    //h = CsD:kD1;
    //h = CsD:kEnumD1;
    //h = CsD:g_d1;
    //h = kD2;
    //h = kEnumD2;
    //h = g_d2;
    //h = CsD:kD2;
    //h = CsD:kEnumD2;
    //h = CsD:g_d2;

    return 0;
}
