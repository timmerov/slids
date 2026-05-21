/*
reopen classes.
*/

/* global scope class definition. */
GsA(int a_ = 0) {
    /* define things. */
    const int kA1 = 1;
    enum EnumA1 ( kEnumA1 );
    global int g_a1 = 2;

    void m_a1() {
        /* access scope things qualifier optional. */
        h = a_ + kA1 + kEnumA1 + g_a1;
        h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
        __println("GsA:m_a1");
    }

    /* derived class from later in host class body. */
    CsB : CsE(int e_ = 22) {
        /* define things. */
        const int kE1 = 23;
        enum EnumE1 ( kEnumE1 );
        global int g_e1 = 24;

        void m_e1() {
            /* access scope things qualifiers optional. */
            h = e_ + kE1 + kEnumE1 + g_e1;
            h = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            h = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            /* access enclosing scope things qualifier optional. */
            h = kA1 + kEnumA1 + g_a1;
            h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            /* access sibling scope things with qualifiers. */
            h = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            h = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            h = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            h = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            __println("CsE:m_e1");
        }
    }

    /* hoisted class definition. */
    CsB(int b_ = 17) {
        /* define things. */
        const int kB1 = 6;
        enum EnumB1 ( kEnumB1 );
        global int g_b1 = 7;

        void m_b1() {
            /* access scope things qualifiers optional. */
            h = b_ + kB1 + kEnumB1 + g_b1;
            h = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            h = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            /* access enclosing scope things qualifier optional. */
            h = kA1 + kEnumA1 + g_a1;
            h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            /* access sibling scope things with qualifiers. */
            h = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            h = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            h = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            h = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            __println("CsB:m_b1");
        }
    }

    /* derived class definition from global class later in file. */
    GsC : CsD(int d_ = 14) {
        /* define things. */
        const int kD1 = 15;
        enum EnumD1 ( kEnumD1 );
        global int g_d1 = 16;

        void m_d1() {
            /* access scope things qualifiers optional. */
            h = d_ + kD1 + kEnumD1 + g_d1;
            h = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            h = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            /* access enclosing scope things qualifier optional. */
            h = kA1 + kEnumA1 + g_a1;
            h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            /* access sibling scope things with qualifiers. */
            h = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            h = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            h = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            h = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            __println("CsD:m_d1");
        }
    }

    /* open an incomplete class. */
    CsF(int f_ = 25, ...) {
        /* define things. */
        const int kF1 = 28;
        enum EnumF1 ( kEnumF1 );
        global int g_f1 = 29;

        void m_f1() {
            /* access scope things qualifiers optional. */
            h = f_ + kF1 + kEnumF1 + g_f1;
            h = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            h = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
            /* access enclosing scope things qualifier optional. */
            h = kA1 + kEnumA1 + g_a1;
            h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            /* access sibling scope things with qualifiers. */
            h = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            h = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            h = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            h = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            h = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            h = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            __println("CsF:m_f1");
        }
    }

    /* re-open an incomplete class. */
    CsF() {
        /* define more things. */
        const int kF2 = 30;
        enum EnumF2 ( kEnumF2 );
        global int g_f2 = 31;

        void m_f2() {
            /* access scope things qualifiers optional. */
            h = f_ + kF1 + kEnumF1 + g_f1;
            h = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            h = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
            h = kF2 + kEnumF2 + g_f2;
            h = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
            h = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
            /* access enclosing scope things qualifier optional. */
            h = kA1 + kEnumA1 + g_a1;
            h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            /* access sibling scope things with qualifiers. */
            h = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            h = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            h = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            h = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            h = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            h = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            __println("CsF:m_f2");
        }
    }

    /* close the incomplete class. */
    CsF(...) {
        /* define more things. */
        const int kF3 = 32;
        enum EnumF3 ( kEnumF3 );
        global int g_f3 = 33;

        void m_f3() {
            /* access scope things qualifiers optional. */
            h = f_ + kF1 + kEnumF1 + g_f1;
            h = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            h = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
            h = kF2 + kEnumF2 + g_f2;
            h = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
            h = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
            h = kF3 + kEnumF3 + g_f3;
            h = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
            h = GsA:CsF:kF3 + GsA:CsF:kEnumF3 + GsA:CsF:g_f3;
            /* access enclosing scope things qualifier optional. */
            h = kA1 + kEnumA1 + g_a1;
            h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            /* access sibling scope things with qualifiers. */
            h = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            h = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            h = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            h = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            h = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            h = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            __println("CsF:m_f3");
        }
    }

    /* open another incomplete class — close lives in GsA reopen. */
    GsG(int g_ = 37, ...) {
        /* define things. */
        const int kG1 = 38;
        enum EnumG1 ( kEnumG1 );
        global int g_g1 = 39;

        void m_g1() {
            /* access scope things qualifiers optional. */
            h = g_ + kG1 + kEnumG1 + g_g1;
            h = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            h = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
            /* access enclosing scope things qualifier optional. */
            h = kA1 + kEnumA1 + g_a1;
            h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            /* access sibling scope things with qualifiers. */
            h = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            h = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            h = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            h = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            h = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            h = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            h = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            h = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
            __println("GsG:m_g1");
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
        h = a_ + kA1 + kEnumA1 + g_a1;
        h = kA2 + kEnumA2 + g_a2;
        h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
        h = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
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
            h = b_ + kB1 + kEnumB1 + g_b1;
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
            h = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            h = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            h = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
            h = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
            __println("CsB:m_b2");
        }
    }

    /* in-class inline reopen. */
    void CsB:m_b3() {
        /* access scope things qualifiers optional. */
        h = b_ + kB1 + kEnumB1 + g_b1;
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
        h = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
        h = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
        h = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
        h = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
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
            h = d_ + kD1 + kEnumD1 + g_d1;
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
            h = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            h = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            h = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
            h = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
            __println("CsD:m_d2");
        }
    }

    /* in-class inline reopen. */
    void CsD:m_d3() {
        /* access scope things qualifiers optional. */
        h = d_ + kD1 + kEnumD1 + g_d1;
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
        h = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
        h = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
        h = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
        h = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
        __println("CsD:m_d3");
    }

    /* reopen derived class. */
    CsE() {
        /* define more things. */
        const int kE2 = 25;
        enum EnumE2 ( kEnumE2 );
        global int g_e2 = 26;

        void m_e2() {
            /* access scope things qualifiers optional. */
            h = e_ + kE1 + kEnumE1 + g_e1;
            h = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            h = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            h = kE2 + kEnumE2 + g_e2;
            h = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
            h = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
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
            h = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            h = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            h = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
            h = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
            __println("CsE:m_e2");
        }
    }

    /* in-class inline reopen. */
    void CsE:m_e3() {
        /* access scope things qualifiers optional. */
        h = e_ + kE1 + kEnumE1 + g_e1;
        h = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
        h = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
        h = kE2 + kEnumE2 + g_e2;
        h = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
        h = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
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
        h = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
        h = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
        h = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
        h = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
        __println("CsE:m_e3");
    }

    /* reopen incomplete class. */
    CsF() {
        /* define more things. */
        const int kF4 = 34;
        enum EnumF4 ( kEnumF4 );
        global int g_f4 = 35;

        void m_f4() {
            /* access scope things qualifiers optional. */
            h = f_ + kF1 + kEnumF1 + g_f1;
            h = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            h = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
            h = kF2 + kEnumF2 + g_f2;
            h = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
            h = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
            h = kF3 + kEnumF3 + g_f3;
            h = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
            h = GsA:CsF:kF3 + GsA:CsF:kEnumF3 + GsA:CsF:g_f3;
            h = kF4 + kEnumF4 + g_f4;
            h = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
            h = GsA:CsF:kF4 + GsA:CsF:kEnumF4 + GsA:CsF:g_f4;
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
            h = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            h = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            h = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
            h = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
            h = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            h = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            h = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
            h = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
            __println("CsF:m_f4");
        }
    }

    /* in-class inline reopen. */
    void CsF:m_f5() {
        /* access scope things qualifiers optional. */
        h = f_ + kF1 + kEnumF1 + g_f1;
        h = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
        h = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
        h = kF2 + kEnumF2 + g_f2;
        h = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
        h = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
        h = kF3 + kEnumF3 + g_f3;
        h = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
        h = GsA:CsF:kF3 + GsA:CsF:kEnumF3 + GsA:CsF:g_f3;
        h = kF4 + kEnumF4 + g_f4;
        h = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
        h = GsA:CsF:kF4 + GsA:CsF:kEnumF4 + GsA:CsF:g_f4;
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
        h = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
        h = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
        h = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
        h = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
        h = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
        h = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
        h = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
        h = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
        __println("CsF:m_f5");
    }

    /* close incomplete class — open lives in GsA primary. */
    GsG(...) {
        /* define more things. */
        const int kG2 = 40;
        enum EnumG2 ( kEnumG2 );
        global int g_g2 = 41;

        void m_g2() {
            /* access scope things qualifiers optional. */
            h = g_ + kG1 + kEnumG1 + g_g1;
            h = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            h = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
            h = kG2 + kEnumG2 + g_g2;
            h = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
            h = GsA:GsG:kG2 + GsA:GsG:kEnumG2 + GsA:GsG:g_g2;
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
            h = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            h = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            h = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
            h = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
            h = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            h = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            h = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
            h = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
            h = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            h = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
            h = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
            h = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
            h = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
            h = GsA:CsF:kF3 + GsA:CsF:kEnumF3 + GsA:CsF:g_f3;
            h = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
            h = GsA:CsF:kF4 + GsA:CsF:kEnumF4 + GsA:CsF:g_f4;
            __println("GsG:m_g2");
        }
    }

    /* compile error: re-definition. */
    //CsB(int b_ = 10) { }
    //CsD(int d_ = 21) { }
    //CsE(int e_ = 27) { }
    //CsF(int f_ = 36) { }
    //GsG(int g_ = 42) { }
}

/* inline reopen. */
void GsA:m_a3() {
    /* access scope things qualifier optional. */
    h = a_ + kA1 + kEnumA1 + g_a1;
    h = kA2 + kEnumA2 + g_a2;
    h = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
    h = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
    __println("GsA:m_a3");
}

/* inline reopen hoisted class. */
void GsA:CsB:m_b4() {
    /* access scope things qualifiers optional. */
    h = b_ + kB1 + kEnumB1 + g_b1;
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
    h = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
    h = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
    h = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
    h = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
    __println("CsB:m_b4");
}

/* inline reopen derived class. */
void CsD:m_d4() {
    /* access scope things qualifiers optional. */
    h = d_ + kD1 + kEnumD1 + g_d1;
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
    h = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
    h = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
    h = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
    h = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
    __println("CsD:m_d4");
}

/* inline reopen derived class. */
void GsA:CsE:m_e4() {
    /* access scope things qualifiers optional. */
    h = e_ + kE1 + kEnumE1 + g_e1;
    h = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
    h = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
    h = kE2 + kEnumE2 + g_e2;
    h = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
    h = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
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
    h = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
    h = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
    h = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
    h = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
    __println("CsE:m_e4");
}

/* inline reopen incomplete class. */
void GsA:CsF:m_f6() {
    /* access scope things qualifiers optional. */
    h = f_ + kF1 + kEnumF1 + g_f1;
    h = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
    h = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
    h = kF2 + kEnumF2 + g_f2;
    h = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
    h = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
    h = kF3 + kEnumF3 + g_f3;
    h = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
    h = GsA:CsF:kF3 + GsA:CsF:kEnumF3 + GsA:CsF:g_f3;
    h = kF4 + kEnumF4 + g_f4;
    h = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
    h = GsA:CsF:kF4 + GsA:CsF:kEnumF4 + GsA:CsF:g_f4;
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
    h = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
    h = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
    h = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
    h = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
    h = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
    h = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
    h = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
    h = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
    __println("CsF:m_f6");
}

/* global scope class definition. */
GsC(int c_ = 11) {
    /* define things. */
    const int kC1 = 12;
    enum EnumC1 ( kEnumC1 );
    global int g_c1 = 13;

    void m_c1() {
        __println("GsC:m_c1");
    }
}

/* compile error: re-definition. */
//GsA(int a_ = 5) { }

void gs_f() {
    /*
    insert lines 5 - 382 here.
    but change the class names.
    */
}

int32 main() {
    {
        GsA gsa;
        h = gsa.a_ + GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
        h = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
        gsa.m_a1();
        gsa.m_a2();
        gsa.m_a3();

        GsA:CsB csb;
        h = csb.b_ + GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
        csb.m_b1();
        csb.m_b2();
        csb.m_b3();
        csb.m_b4();

        GsA:CsD csd;
        h = csd.d_ + GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
        csd.m_d1();
        csd.m_d2();
        csd.m_d3();
        csd.m_d4();

        GsA:CsE cse;
        h = cse.e_ + GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
        cse.m_e1();
        cse.m_e2();
        cse.m_e3();
        cse.m_e4();

        GsA:CsF csf;
        h = csf.f_ + GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
        csf.m_f1();
        csf.m_f2();
        csf.m_f3();
        csf.m_f4();
        csf.m_f5();
        csf.m_f6();

        GsA:GsG gsg;
        h = gsg.g_ + GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
        gsg.m_g1();
        gsg.m_g2();
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
