/*
reopen classes in function bodies.
*/

/* global scope class definition. */
void gs_func() {
    /*
    /* define things. */
    const int kA1 = 1;
    enum EnumA1 ( kEnumA1 );
    global int g_a1 = 2;
    alias intA = int;

    /* derived class from later in host class body. */
    CsB : CsE(int e_ = 22) {
        /* define things. */
        const int kE1 = 23;
        enum EnumE1 ( kEnumE1 );
        global int g_e1 = 24;
        alias intE = int;

        void m_e1() {
            /* access scope things qualifiers optional. */
            intE e = e_ + kE1 + kEnumE1 + g_e1;
            CsE:intE ce = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            /* access sibling scope things with qualifiers. */
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            /* access base scope things qualifiers optional. */
            intB b = kB1 + kEnumB1 + g_b1;
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            CsE:intB ceb = CsE:kB1 + CsE:kEnumB1 + CsE:g_b1;
            __println("CsE:m_e1");
        }
    }

    /* hoisted class definition. */
    CsB(int b_ = 17) {
        /* define things. */
        const int kB1 = 6;
        enum EnumB1 ( kEnumB1 );
        global int g_b1 = 7;
        alias intB = int;

        void m_b1() {
            /* access scope things qualifiers optional. */
            intB b = b_ + kB1 + kEnumB1 + g_b1;
            CsB:intB cb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            /* access sibling scope things with qualifiers. */
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            __println("CsB:m_b1");
        }
    }

    /* derived class definition from global class later in file. */
    GsC : CsD(int d_ = 14) {
        /* define things. */
        const int kD1 = 15;
        enum EnumD1 ( kEnumD1 );
        global int g_d1 = 16;
        alias intD = int;

        void m_d1() {
            /* access scope things qualifiers optional. */
            intD d = d_ + kD1 + kEnumD1 + g_d1;
            CsD:intD cd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            /* access sibling scope things with qualifiers. */
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            /* access base scope things qualifiers optional. */
            intC c = kC1 + kEnumC1 + g_c1;
            GsC:intC cc = GsC:kC1 + GsC:kEnumC1 + GsC:g_c1;
            CsD:intC cdc = CsD:kC1 + CsD:kEnumC1 + CsD:g_c1;
            __println("CsD:m_d1");
        }
    }

    /* open an incomplete class. */
    CsF(int f_ = 25, ...) {
        /* define things. */
        const int kF1 = 28;
        enum EnumF1 ( kEnumF1 );
        global int g_f1 = 29;
        alias intF = int;

        void m_f1() {
            /* access scope things qualifiers optional. */
            intF f = f_ + kF1 + kEnumF1 + g_f1;
            CsF:intF cf = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            /* access sibling scope things with qualifiers. */
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
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
            intF f = f_ + kF1 + kEnumF1 + g_f1;
            CsF:intF cf = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            f = kF2 + kEnumF2 + g_f2;
            cf = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            /* access sibling scope things with qualifiers. */
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
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
            intF f = f_ + kF1 + kEnumF1 + g_f1;
            CsF:intF cf = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            f = kF2 + kEnumF2 + g_f2;
            cf = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
            f = kF3 + kEnumF3 + g_f3;
            cf = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            /* access sibling scope things with qualifiers. */
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            __println("CsF:m_f3");
        }
    }

    GsG(int g_ = 37, ...) {
        /* define things. */
        const int kG1 = 38;
        enum EnumG1 ( kEnumG1 );
        global int g_g1 = 39;
        alias intG = int;

        void m_g1() {
            /* access scope things qualifiers optional. */
            intG g = g_ + kG1 + kEnumG1 + g_g1;
            GsG:intG cg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            /* access sibling scope things with qualifiers. */
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            __println("GsG:m_g1");
        }
    }

    /* reopen hoisted class. */
    CsB() {
        /* define more things. */
        const int kB2 = 8;
        enum EnumB2 ( kEnumB2 );
        global int g_b2 = 9;

        void m_b2() {
            /* access scope things qualifiers optional. */
            intB b = b_ + kB1 + kEnumB1 + g_b1;
            CsB:intB cb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            b = kB2 + kEnumB2 + g_b2;
            cb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            a = kA2 + kEnumA2 + g_a2;
            /* access sibling scope things with qualifiers. */
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            dd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            ee = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
            CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            ff = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
            ff = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
            ff = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            /* access base scope things qualifiers optional. */
            /* CsB has no base. */
            __println("CsB:m_b2");
        }

        /* op overload added via reopen — exercises op-method through the
           reopen merge path. */
        op+(CsB a, CsB b) {
            __println("CsB:op+");
        }
    }

    /* in-class inline reopen. */
    void CsB:m_b3() {
        /* access scope things qualifiers optional. */
        intB b = b_ + kB1 + kEnumB1 + g_b1;
        CsB:intB cb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
        b = kB2 + kEnumB2 + g_b2;
        cb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
        /* access enclosing scope things qualifier optional. */
        intA a = kA1 + kEnumA1 + g_a1;
        a = kA2 + kEnumA2 + g_a2;
        /* access sibling scope things with qualifiers. */
        CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
        dd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
        CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
        ee = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
        CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
        ff = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
        ff = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
        ff = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
        GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
        gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
        GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
        /* access base scope things qualifiers optional. */
        /* CsB has no base. */
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
            intD d = d_ + kD1 + kEnumD1 + g_d1;
            CsD:intD cd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            d = kD2 + kEnumD2 + g_d2;
            cd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            a = kA2 + kEnumA2 + g_a2;
            /* access sibling scope things with qualifiers. */
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            bb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            ee = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
            CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            ff = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
            ff = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
            ff = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            /* access base scope things qualifiers optional. */
            intC c = kC1 + kEnumC1 + g_c1;
            GsC:intC cc = GsC:kC1 + GsC:kEnumC1 + GsC:g_c1;
            c = kC2 + kEnumC2 + g_c2;
            cc = GsC:kC2 + GsC:kEnumC2 + GsC:g_c2;
            __println("CsD:m_d2");
        }
    }

    /* in-class inline reopen. */
    void CsD:m_d3() {
        /* access scope things qualifiers optional. */
        intD d = d_ + kD1 + kEnumD1 + g_d1;
        CsD:intD cd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
        d = kD2 + kEnumD2 + g_d2;
        cd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
        /* access enclosing scope things qualifier optional. */
        intA a = kA1 + kEnumA1 + g_a1;
        a = kA2 + kEnumA2 + g_a2;
        /* access sibling scope things with qualifiers. */
        CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
        bb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
        CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
        ee = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
        CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
        ff = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
        ff = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
        ff = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
        GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
        gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
        GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
        /* access base scope things qualifiers optional. */
        intC c = kC1 + kEnumC1 + g_c1;
        GsC:intC cc = GsC:kC1 + GsC:kEnumC1 + GsC:g_c1;
        c = kC2 + kEnumC2 + g_c2;
        cc = GsC:kC2 + GsC:kEnumC2 + GsC:g_c2;
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
            intE e = e_ + kE1 + kEnumE1 + g_e1;
            CsE:intE ce = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            e = kE2 + kEnumE2 + g_e2;
            ce = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            a = kA2 + kEnumA2 + g_a2;
            /* access sibling scope things with qualifiers. */
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            bb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            dd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
            CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            ff = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
            ff = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
            ff = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            __println("CsE:m_e2");
        }
    }

    /* in-class inline reopen. */
    void CsE:m_e3() {
        /* access scope things qualifiers optional. */
        intE e = e_ + kE1 + kEnumE1 + g_e1;
        CsE:intE ce = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
        e = kE2 + kEnumE2 + g_e2;
        ce = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
        /* access enclosing scope things qualifier optional. */
        intA a = kA1 + kEnumA1 + g_a1;
        a = kA2 + kEnumA2 + g_a2;
        /* access sibling scope things with qualifiers. */
        CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
        bb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
        CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
        dd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
        CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
        ff = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
        ff = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
        ff = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
        GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
        gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
        GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
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
            intF f = f_ + kF1 + kEnumF1 + g_f1;
            CsF:intF cf = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            f = kF2 + kEnumF2 + g_f2;
            cf = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
            f = kF3 + kEnumF3 + g_f3;
            cf = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
            f = kF4 + kEnumF4 + g_f4;
            cf = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            a = kA2 + kEnumA2 + g_a2;
            /* access sibling scope things with qualifiers. */
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            bb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            dd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            ee = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            __println("CsF:m_f4");
        }
    }

    /* in-class inline reopen. */
    void CsF:m_f5() {
        /* access scope things qualifiers optional. */
        intF f = f_ + kF1 + kEnumF1 + g_f1;
        CsF:intF cf = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
        f = kF2 + kEnumF2 + g_f2;
        cf = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
        f = kF3 + kEnumF3 + g_f3;
        cf = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
        f = kF4 + kEnumF4 + g_f4;
        cf = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
        /* access enclosing scope things qualifier optional. */
        intA a = kA1 + kEnumA1 + g_a1;
        a = kA2 + kEnumA2 + g_a2;
        /* access sibling scope things with qualifiers. */
        CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
        bb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
        CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
        dd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
        CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
        ee = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
        GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
        gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
        GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
        __println("CsF:m_f5");
    }

    GsG(...) {
        /* define more things. */
        const int kG2 = 40;
        enum EnumG2 ( kEnumG2 );
        global int g_g2 = 41;

        void m_g2() {
            /* access scope things qualifiers optional. */
            intG g = g_ + kG1 + kEnumG1 + g_g1;
            GsG:intG cg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            g = kG2 + kEnumG2 + g_g2;
            cg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            a = kA2 + kEnumA2 + g_a2;
            /* access sibling scope things with qualifiers. */
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            bb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            dd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            ee = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
            CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            ff = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
            ff = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
            ff = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            __println("GsG:m_g2");
        }
    }
    */

    GsH(int h_ = 43) {
        /* define things. */
        const int kH1 = 44;
        enum EnumH1 ( kEnumH1 );
        global int g_h1 = 45;
        alias intH = int;

        void m_h1() {
            /*
            /* access scope things qualifiers optional. */
            intH h = h_ + kH1 + kEnumH1 + g_h1;
            GsH:intH ch = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            a = kA2 + kEnumA2 + g_a2;
            /* access sibling scope things with qualifiers. */
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            bb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            dd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            ee = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
            CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            ff = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
            ff = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
            ff = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
            */
            __println("GsH:m_h1");
        }
    }

    GsH() {
        /* define more things. */
        const int kH2 = 47;
        enum EnumH2 ( kEnumH2 );
        global int g_h2 = 48;

        void m_h2() {
            /* access scope things qualifiers optional. */
            intH h = h_ + kH1 + kEnumH1 + g_h1;
            GsH:intH ch = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            h = kH2 + kEnumH2 + g_h2;
            ch = GsH:kH2 + GsH:kEnumH2 + GsH:g_h2;
            __println("GsH:m_h2");
        }
    }

    void GsH:m_h3() {
        /* access scope things qualifiers optional. */
        intH h = h_ + kH1 + kEnumH1 + g_h1;
        GsH:intH ch = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
        h = kH2 + kEnumH2 + g_h2;
        ch = GsH:kH2 + GsH:kEnumH2 + GsH:g_h2;
        __println("GsH:m_h3");
    }

    /* negatives: nested-class with new fields after the class is already complete. */
    //-EXPECT-ERROR: is already complete
    //CsB(int b_ = 10) { }
    //-EXPECT-ERROR: is already complete
    //CsD(int d_ = 21) { }
    //-EXPECT-ERROR: is already complete
    //CsE(int e_ = 27) { }
    //-EXPECT-ERROR: is already complete
    //CsF(int f_ = 36) { }
    //-EXPECT-ERROR: is already complete
    //GsG(int g_ = 42) { }
    //-EXPECT-ERROR: is already complete
    //GsH(int h_ = 46) { }
}

/* global scope class definition. */
GsC(int c_ = 11) {
    /* define things. */
    const int kC1 = 12;
    enum EnumC1 ( kEnumC1 );
    global int g_c1 = 13;
    alias intC = int;

    void m_c1() {
        /* access scope things qualifier optional. */
        intC c = c_ + kC1 + kEnumC1 + g_c1;
        GsC:intC cc = GsC:kC1 + GsC:kEnumC1 + GsC:g_c1;
        __println("GsC:m_c1");
    }
}

/* reopen global scope class. */
GsC() {
    /* define more things. */
    const int kC2 = 51;
    enum EnumC2 ( kEnumC2 );
    global int g_c2 = 52;

    void m_c2() {
        /* access scope things qualifier optional. */
        intC c = c_ + kC1 + kEnumC1 + g_c1;
        c = kC2 + kEnumC2 + g_c2;
        GsC:intC cc = GsC:kC1 + GsC:kEnumC1 + GsC:g_c1;
        cc = GsC:kC2 + GsC:kEnumC2 + GsC:g_c2;
        __println("GsC:m_c2");
    }
}

int32 main() {
    {
        gs_foo();

        GsC gsc;
        h = gsc.c_ + GsC:kC1 + GsC:kEnumC1 + GsC:g_c1;
        h = GsC:kC2 + GsC:kEnumC2 + GsC:g_c2;
        gsc.m_c1();
        gsc.m_c2();
    }

    //-EXPECT-ERROR: Undefined variable: kA1
    //h = kA1;
    //-EXPECT-ERROR: Undefined variable: kEnumA1
    //h = kEnumA1;
    //-EXPECT-ERROR: Undefined variable: g_a1
    //h = g_a1;
    //-EXPECT-ERROR: Undefined variable: kB1
    //h = kB1;
    //-EXPECT-ERROR: Undefined variable: kEnumB1
    //h = kEnumB1;
    //-EXPECT-ERROR: Undefined variable: g_b1
    //h = g_b1;
    //-EXPECT-ERROR: Undefined variable: kB2
    //h = kB2;
    //-EXPECT-ERROR: Undefined variable: kEnumB2
    //h = kEnumB2;
    //-EXPECT-ERROR: Undefined variable: g_b2
    //h = g_b2;
    //-EXPECT-ERROR: Undefined variable: kC1
    //h = kC1;
    //-EXPECT-ERROR: Undefined variable: kEnumC1
    //h = kEnumC1;
    //-EXPECT-ERROR: Undefined variable: g_c1
    //h = g_c1;
    //-EXPECT-ERROR: Undefined variable: kC2
    //h = kC2;
    //-EXPECT-ERROR: Undefined variable: kEnumC2
    //h = kEnumC2;
    //-EXPECT-ERROR: Undefined variable: g_c2
    //h = g_c2;
    //-EXPECT-ERROR: Undefined variable: kD1
    //h = kD1;
    //-EXPECT-ERROR: Undefined variable: kEnumD1
    //h = kEnumD1;
    //-EXPECT-ERROR: Undefined variable: g_d1
    //h = g_d1;
    //-EXPECT-ERROR: Undefined variable: kD2
    //h = kD2;
    //-EXPECT-ERROR: Undefined variable: kEnumD2
    //h = kEnumD2;
    //-EXPECT-ERROR: Undefined variable: g_d2
    //h = g_d2;
    //-EXPECT-ERROR: Undefined variable: kE1
    //h = kE1;
    //-EXPECT-ERROR: Undefined variable: kEnumE1
    //h = kEnumE1;
    //-EXPECT-ERROR: Undefined variable: g_e1
    //h = g_e1;
    //-EXPECT-ERROR: Undefined variable: kE2
    //h = kE2;
    //-EXPECT-ERROR: Undefined variable: kEnumE2
    //h = kEnumE2;
    //-EXPECT-ERROR: Undefined variable: g_e2
    //h = g_e2;
    //-EXPECT-ERROR: Undefined variable: kF1
    //h = kF1;
    //-EXPECT-ERROR: Undefined variable: kEnumF1
    //h = kEnumF1;
    //-EXPECT-ERROR: Undefined variable: g_f1
    //h = g_f1;
    //-EXPECT-ERROR: Undefined variable: kF2
    //h = kF2;
    //-EXPECT-ERROR: Undefined variable: kEnumF2
    //h = kEnumF2;
    //-EXPECT-ERROR: Undefined variable: g_f2
    //h = g_f2;
    //-EXPECT-ERROR: Undefined variable: kF3
    //h = kF3;
    //-EXPECT-ERROR: Undefined variable: kEnumF3
    //h = kEnumF3;
    //-EXPECT-ERROR: Undefined variable: g_f3
    //h = g_f3;
    //-EXPECT-ERROR: Undefined variable: kF4
    //h = kF4;
    //-EXPECT-ERROR: Undefined variable: kEnumF4
    //h = kEnumF4;
    //-EXPECT-ERROR: Undefined variable: g_f4
    //h = g_f4;
    //-EXPECT-ERROR: Undefined variable: kG1
    //h = kG1;
    //-EXPECT-ERROR: Undefined variable: kEnumG1
    //h = kEnumG1;
    //-EXPECT-ERROR: Undefined variable: g_g1
    //h = g_g1;
    //-EXPECT-ERROR: Undefined variable: kG2
    //h = kG2;
    //-EXPECT-ERROR: Undefined variable: kEnumG2
    //h = kEnumG2;
    //-EXPECT-ERROR: Undefined variable: g_g2
    //h = g_g2;
    //-EXPECT-ERROR: Undefined variable: kH1
    //h = kH1;
    //-EXPECT-ERROR: Undefined variable: kEnumH1
    //h = kEnumH1;
    //-EXPECT-ERROR: Undefined variable: g_h1
    //h = g_h1;
    //-EXPECT-ERROR: Undefined variable: kH2
    //h = kH2;
    //-EXPECT-ERROR: Undefined variable: kEnumH2
    //h = kEnumH2;
    //-EXPECT-ERROR: Undefined variable: g_h2
    //h = g_h2;
    return 0;
}
