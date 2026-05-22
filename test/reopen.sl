/*
reopen classes.
*/

/* global scope class definition. */
GsA(int a_ = 0) {
    /* define things. */
    const int kA1 = 1;
    enum EnumA1 ( kEnumA1 );
    global int g_a1 = 2;
    alias intA = int;

    void m_a1() {
        /* access scope things qualifier optional. */
        intA a = a_ + kA1 + kEnumA1 + g_a1;
        GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
        __println("GsA:m_a1");
    }

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
            GsA:CsE:intE ace = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            /* access sibling scope things with qualifiers. */
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            GsA:CsD:intD add = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            GsA:CsF:intF aff = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            GsA:GsG:intG agg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            GsA:GsH:intH ahh = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
            /* access base scope things qualifiers optional. */
            intB b = kB1 + kEnumB1 + g_b1;
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            CsE:intB ceb = CsE:kB1 + CsE:kEnumB1 + CsE:g_b1;
            GsA:CsE:intB aceb = GsA:CsE:kB1 + GsA:CsE:kEnumB1 + GsA:CsE:g_b1;
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
            GsA:CsB:intB acb = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            /* access sibling scope things with qualifiers. */
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            GsA:CsD:intD add = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            GsA:CsE:intE aee = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            GsA:CsF:intF aff = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            GsA:GsG:intG agg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            GsA:GsH:intH ahh = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
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
            GsA:CsD:intD acd = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            /* access sibling scope things with qualifiers. */
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            GsA:CsB:intB abb = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            GsA:CsE:intE aee = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            GsA:CsF:intF aff = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            GsA:GsG:intG agg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            GsA:GsH:intH ahh = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
            /* access base scope things qualifiers optional. */
            intC c = kC1 + kEnumC1 + g_c1;
            GsC:intC cc = GsC:kC1 + GsC:kEnumC1 + GsC:g_c1;
            CsD:intC cdc = CsD:kC1 + CsD:kEnumC1 + CsD:g_c1;
            GsA:CsD:intC acdc = GsA:CsD:kC1 + GsA:CsD:kEnumC1 + GsA:CsD:g_c1;
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
            GsA:CsF:intF acf = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            /* access sibling scope things with qualifiers. */
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            GsA:CsB:intB abb = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            GsA:CsD:intD add = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            GsA:CsE:intE aee = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            GsA:GsG:intG agg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            GsA:GsH:intH ahh = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
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
            GsA:CsF:intF acf = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
            f = kF2 + kEnumF2 + g_f2;
            cf = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
            acf = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            /* access sibling scope things with qualifiers. */
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            GsA:CsB:intB abb = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            GsA:CsD:intD add = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            GsA:CsE:intE aee = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            GsA:GsG:intG agg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            GsA:GsH:intH ahh = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
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
            GsA:CsF:intF acf = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
            f = kF2 + kEnumF2 + g_f2;
            cf = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
            acf = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
            f = kF3 + kEnumF3 + g_f3;
            cf = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
            acf = GsA:CsF:kF3 + GsA:CsF:kEnumF3 + GsA:CsF:g_f3;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            /* access sibling scope things with qualifiers. */
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            GsA:CsB:intB abb = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            GsA:CsD:intD add = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            GsA:CsE:intE aee = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            GsA:GsG:intG agg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            GsA:GsH:intH ahh = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
            __println("CsF:m_f3");
        }
    }

    /* open another incomplete class — close lives in GsA reopen. */
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
            GsA:GsG:intG acg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            /* access sibling scope things with qualifiers. */
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            GsA:CsB:intB abb = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            GsA:CsD:intD add = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            GsA:CsE:intE aee = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            GsA:CsF:intF aff = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            GsA:GsH:intH ahh = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
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
        intA a = a_ + kA1 + kEnumA1 + g_a1;
        a = kA2 + kEnumA2 + g_a2;
        GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
        aa = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
        __println("GsA:m_a2");
    }

    /* negatives: re-definition inside a reopen of the same class. */
    //-EXPECT-ERROR: kA1
    //const int kA1 = 1;
    //-EXPECT-ERROR: Class 'GsA' has a duplicate enum 'EnumA1'.
    //enum EnumA1 ( kEnumA1 );
    //-EXPECT-ERROR: redeclares field 'g_a1'
    //global int g_a1 = 2;
    //-EXPECT-ERROR: duplicate method 'm_a1'
    //void m_a1() { }

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
            GsA:CsB:intB acb = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            b = kB2 + kEnumB2 + g_b2;
            cb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
            acb = GsA:CsB:kB2 + GsA:CsB:kEnumB2 + GsA:CsB:g_b2;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            a = kA2 + kEnumA2 + g_a2;
            aa = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
            /* access sibling scope things with qualifiers. */
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            GsA:CsD:intD add = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            dd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
            add = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            GsA:CsE:intE aee = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            ee = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
            aee = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
            CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            GsA:CsF:intF aff = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
            ff = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
            aff = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
            ff = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
            aff = GsA:CsF:kF3 + GsA:CsF:kEnumF3 + GsA:CsF:g_f3;
            ff = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
            aff = GsA:CsF:kF4 + GsA:CsF:kEnumF4 + GsA:CsF:g_f4;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            GsA:GsG:intG agg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
            gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
            agg = GsA:GsG:kG2 + GsA:GsG:kEnumG2 + GsA:GsG:g_g2;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            GsA:GsH:intH ahh = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
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
        GsA:CsB:intB acb = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
        b = kB2 + kEnumB2 + g_b2;
        cb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
        acb = GsA:CsB:kB2 + GsA:CsB:kEnumB2 + GsA:CsB:g_b2;
        /* access enclosing scope things qualifier optional. */
        intA a = kA1 + kEnumA1 + g_a1;
        GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
        a = kA2 + kEnumA2 + g_a2;
        aa = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
        /* access sibling scope things with qualifiers. */
        CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
        GsA:CsD:intD add = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
        dd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
        add = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
        CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
        GsA:CsE:intE aee = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
        ee = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
        aee = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
        CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
        GsA:CsF:intF aff = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
        ff = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
        aff = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
        ff = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
        aff = GsA:CsF:kF3 + GsA:CsF:kEnumF3 + GsA:CsF:g_f3;
        ff = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
        aff = GsA:CsF:kF4 + GsA:CsF:kEnumF4 + GsA:CsF:g_f4;
        GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
        GsA:GsG:intG agg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
        gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
        agg = GsA:GsG:kG2 + GsA:GsG:kEnumG2 + GsA:GsG:g_g2;
        GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
        GsA:GsH:intH ahh = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
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
            GsA:CsD:intD acd = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            d = kD2 + kEnumD2 + g_d2;
            cd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
            acd = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            a = kA2 + kEnumA2 + g_a2;
            aa = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
            /* access sibling scope things with qualifiers. */
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            GsA:CsB:intB abb = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            bb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
            abb = GsA:CsB:kB2 + GsA:CsB:kEnumB2 + GsA:CsB:g_b2;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            GsA:CsE:intE aee = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            ee = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
            aee = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
            CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            GsA:CsF:intF aff = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
            ff = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
            aff = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
            ff = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
            aff = GsA:CsF:kF3 + GsA:CsF:kEnumF3 + GsA:CsF:g_f3;
            ff = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
            aff = GsA:CsF:kF4 + GsA:CsF:kEnumF4 + GsA:CsF:g_f4;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            GsA:GsG:intG agg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
            gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
            agg = GsA:GsG:kG2 + GsA:GsG:kEnumG2 + GsA:GsG:g_g2;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            GsA:GsH:intH ahh = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
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
        GsA:CsD:intD acd = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
        d = kD2 + kEnumD2 + g_d2;
        cd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
        acd = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
        /* access enclosing scope things qualifier optional. */
        intA a = kA1 + kEnumA1 + g_a1;
        GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
        a = kA2 + kEnumA2 + g_a2;
        aa = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
        /* access sibling scope things with qualifiers. */
        CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
        GsA:CsB:intB abb = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
        bb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
        abb = GsA:CsB:kB2 + GsA:CsB:kEnumB2 + GsA:CsB:g_b2;
        CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
        GsA:CsE:intE aee = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
        ee = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
        aee = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
        CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
        GsA:CsF:intF aff = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
        ff = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
        aff = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
        ff = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
        aff = GsA:CsF:kF3 + GsA:CsF:kEnumF3 + GsA:CsF:g_f3;
        ff = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
        aff = GsA:CsF:kF4 + GsA:CsF:kEnumF4 + GsA:CsF:g_f4;
        GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
        GsA:GsG:intG agg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
        gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
        agg = GsA:GsG:kG2 + GsA:GsG:kEnumG2 + GsA:GsG:g_g2;
        GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
        GsA:GsH:intH ahh = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
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
            GsA:CsE:intE ace = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            e = kE2 + kEnumE2 + g_e2;
            ce = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
            ace = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            a = kA2 + kEnumA2 + g_a2;
            aa = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
            /* access sibling scope things with qualifiers. */
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            GsA:CsB:intB abb = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            bb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
            abb = GsA:CsB:kB2 + GsA:CsB:kEnumB2 + GsA:CsB:g_b2;
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            GsA:CsD:intD add = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            dd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
            add = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
            CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            GsA:CsF:intF aff = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
            ff = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
            aff = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
            ff = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
            aff = GsA:CsF:kF3 + GsA:CsF:kEnumF3 + GsA:CsF:g_f3;
            ff = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
            aff = GsA:CsF:kF4 + GsA:CsF:kEnumF4 + GsA:CsF:g_f4;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            GsA:GsG:intG agg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
            gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
            agg = GsA:GsG:kG2 + GsA:GsG:kEnumG2 + GsA:GsG:g_g2;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            GsA:GsH:intH ahh = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
            __println("CsE:m_e2");
        }
    }

    /* in-class inline reopen. */
    void CsE:m_e3() {
        /* access scope things qualifiers optional. */
        intE e = e_ + kE1 + kEnumE1 + g_e1;
        CsE:intE ce = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
        GsA:CsE:intE ace = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
        e = kE2 + kEnumE2 + g_e2;
        ce = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
        ace = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
        /* access enclosing scope things qualifier optional. */
        intA a = kA1 + kEnumA1 + g_a1;
        GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
        a = kA2 + kEnumA2 + g_a2;
        aa = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
        /* access sibling scope things with qualifiers. */
        CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
        GsA:CsB:intB abb = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
        bb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
        abb = GsA:CsB:kB2 + GsA:CsB:kEnumB2 + GsA:CsB:g_b2;
        CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
        GsA:CsD:intD add = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
        dd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
        add = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
        CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
        GsA:CsF:intF aff = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
        ff = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
        aff = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
        ff = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
        aff = GsA:CsF:kF3 + GsA:CsF:kEnumF3 + GsA:CsF:g_f3;
        ff = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
        aff = GsA:CsF:kF4 + GsA:CsF:kEnumF4 + GsA:CsF:g_f4;
        GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
        GsA:GsG:intG agg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
        gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
        agg = GsA:GsG:kG2 + GsA:GsG:kEnumG2 + GsA:GsG:g_g2;
        GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
        GsA:GsH:intH ahh = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
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
            GsA:CsF:intF acf = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
            f = kF2 + kEnumF2 + g_f2;
            cf = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
            acf = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
            f = kF3 + kEnumF3 + g_f3;
            cf = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
            acf = GsA:CsF:kF3 + GsA:CsF:kEnumF3 + GsA:CsF:g_f3;
            f = kF4 + kEnumF4 + g_f4;
            cf = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
            acf = GsA:CsF:kF4 + GsA:CsF:kEnumF4 + GsA:CsF:g_f4;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            a = kA2 + kEnumA2 + g_a2;
            aa = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
            /* access sibling scope things with qualifiers. */
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            GsA:CsB:intB abb = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            bb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
            abb = GsA:CsB:kB2 + GsA:CsB:kEnumB2 + GsA:CsB:g_b2;
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            GsA:CsD:intD add = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            dd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
            add = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            GsA:CsE:intE aee = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            ee = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
            aee = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            GsA:GsG:intG agg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
            gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
            agg = GsA:GsG:kG2 + GsA:GsG:kEnumG2 + GsA:GsG:g_g2;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            GsA:GsH:intH ahh = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
            __println("CsF:m_f4");
        }
    }

    /* in-class inline reopen. */
    void CsF:m_f5() {
        /* access scope things qualifiers optional. */
        intF f = f_ + kF1 + kEnumF1 + g_f1;
        CsF:intF cf = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
        GsA:CsF:intF acf = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
        f = kF2 + kEnumF2 + g_f2;
        cf = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
        acf = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
        f = kF3 + kEnumF3 + g_f3;
        cf = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
        acf = GsA:CsF:kF3 + GsA:CsF:kEnumF3 + GsA:CsF:g_f3;
        f = kF4 + kEnumF4 + g_f4;
        cf = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
        acf = GsA:CsF:kF4 + GsA:CsF:kEnumF4 + GsA:CsF:g_f4;
        /* access enclosing scope things qualifier optional. */
        intA a = kA1 + kEnumA1 + g_a1;
        GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
        a = kA2 + kEnumA2 + g_a2;
        aa = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
        /* access sibling scope things with qualifiers. */
        CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
        GsA:CsB:intB abb = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
        bb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
        abb = GsA:CsB:kB2 + GsA:CsB:kEnumB2 + GsA:CsB:g_b2;
        CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
        GsA:CsD:intD add = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
        dd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
        add = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
        CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
        GsA:CsE:intE aee = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
        ee = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
        aee = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
        GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
        GsA:GsG:intG agg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
        gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
        agg = GsA:GsG:kG2 + GsA:GsG:kEnumG2 + GsA:GsG:g_g2;
        GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
        GsA:GsH:intH ahh = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
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
            intG g = g_ + kG1 + kEnumG1 + g_g1;
            GsG:intG cg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            GsA:GsG:intG acg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
            g = kG2 + kEnumG2 + g_g2;
            cg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
            acg = GsA:GsG:kG2 + GsA:GsG:kEnumG2 + GsA:GsG:g_g2;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            a = kA2 + kEnumA2 + g_a2;
            aa = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
            /* access sibling scope things with qualifiers. */
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            GsA:CsB:intB abb = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            bb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
            abb = GsA:CsB:kB2 + GsA:CsB:kEnumB2 + GsA:CsB:g_b2;
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            GsA:CsD:intD add = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            dd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
            add = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            GsA:CsE:intE aee = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            ee = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
            aee = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
            CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            GsA:CsF:intF aff = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
            ff = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
            aff = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
            ff = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
            aff = GsA:CsF:kF3 + GsA:CsF:kEnumF3 + GsA:CsF:g_f3;
            ff = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
            aff = GsA:CsF:kF4 + GsA:CsF:kEnumF4 + GsA:CsF:g_f4;
            GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            GsA:GsH:intH ahh = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
            __println("GsG:m_g2");
        }
    }

    /* new nested class introduced by the reopen — never seen in GsA's primary. */
    GsH(int h_ = 43) {
        /* define things. */
        const int kH1 = 44;
        enum EnumH1 ( kEnumH1 );
        global int g_h1 = 45;
        alias intH = int;

        void m_h1() {
            /* access scope things qualifiers optional. */
            intH h = h_ + kH1 + kEnumH1 + g_h1;
            GsH:intH ch = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            GsA:GsH:intH ach = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
            /* access enclosing scope things qualifier optional. */
            intA a = kA1 + kEnumA1 + g_a1;
            GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
            a = kA2 + kEnumA2 + g_a2;
            aa = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
            /* access sibling scope things with qualifiers. */
            CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
            GsA:CsB:intB abb = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
            bb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
            abb = GsA:CsB:kB2 + GsA:CsB:kEnumB2 + GsA:CsB:g_b2;
            CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
            GsA:CsD:intD add = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
            dd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
            add = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
            CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
            GsA:CsE:intE aee = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
            ee = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
            aee = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
            CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
            GsA:CsF:intF aff = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
            ff = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
            aff = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
            ff = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
            aff = GsA:CsF:kF3 + GsA:CsF:kEnumF3 + GsA:CsF:g_f3;
            ff = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
            aff = GsA:CsF:kF4 + GsA:CsF:kEnumF4 + GsA:CsF:g_f4;
            GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
            GsA:GsG:intG agg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
            gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
            agg = GsA:GsG:kG2 + GsA:GsG:kEnumG2 + GsA:GsG:g_g2;
            __println("GsH:m_h1");
        }
    }

    /* block-mode reopen of the new class inside GsA reopen. */
    GsH() {
        /* define more things. */
        const int kH2 = 47;
        enum EnumH2 ( kEnumH2 );
        global int g_h2 = 48;

        void m_h2() {
            /* access scope things qualifiers optional. */
            intH h = h_ + kH1 + kEnumH1 + g_h1;
            GsH:intH ch = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
            GsA:GsH:intH ach = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
            h = kH2 + kEnumH2 + g_h2;
            ch = GsH:kH2 + GsH:kEnumH2 + GsH:g_h2;
            ach = GsA:GsH:kH2 + GsA:GsH:kEnumH2 + GsA:GsH:g_h2;
            __println("GsH:m_h2");
        }
    }

    /* inline-mode reopen of the new class inside GsA reopen. */
    void GsH:m_h3() {
        /* access scope things qualifiers optional. */
        intH h = h_ + kH1 + kEnumH1 + g_h1;
        GsH:intH ch = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
        GsA:GsH:intH ach = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
        h = kH2 + kEnumH2 + g_h2;
        ch = GsH:kH2 + GsH:kEnumH2 + GsH:g_h2;
        ach = GsA:GsH:kH2 + GsA:GsH:kEnumH2 + GsA:GsH:g_h2;
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

/* inline reopen. */
void GsA:m_a3() {
    /* access scope things qualifier optional. */
    intA a = a_ + kA1 + kEnumA1 + g_a1;
    a = kA2 + kEnumA2 + g_a2;
    GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
    aa = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
    __println("GsA:m_a3");
}

/* second file-scope reopen of GsA — chained reopens. */
GsA() {
    /* define more things. */
    const int kA3 = 56;
    enum EnumA3 ( kEnumA3 );
    global int g_a3 = 57;

    void m_a4() {
        /* access scope things qualifier optional. */
        intA a = a_ + kA1 + kEnumA1 + g_a1;
        a = kA2 + kEnumA2 + g_a2;
        a = kA3 + kEnumA3 + g_a3;
        GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
        aa = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
        aa = GsA:kA3 + GsA:kEnumA3 + GsA:g_a3;
        __println("GsA:m_a4");
    }
}

/* empty reopen — bare `Class() {}` with no body content. */
GsA() {
}

/* inline reopen hoisted class. */
void GsA:CsB:m_b4() {
    /* access scope things qualifiers optional. */
    intB b = b_ + kB1 + kEnumB1 + g_b1;
    CsB:intB cb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
    GsA:CsB:intB acb = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
    b = kB2 + kEnumB2 + g_b2;
    cb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
    acb = GsA:CsB:kB2 + GsA:CsB:kEnumB2 + GsA:CsB:g_b2;
    /* access enclosing scope things qualifier optional. */
    intA a = kA1 + kEnumA1 + g_a1;
    GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
    a = kA2 + kEnumA2 + g_a2;
    aa = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
    /* access sibling scope things with qualifiers. */
    CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
    GsA:CsD:intD add = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
    dd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
    add = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
    CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
    GsA:CsE:intE aee = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
    ee = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
    aee = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
    CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
    GsA:CsF:intF aff = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
    ff = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
    aff = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
    ff = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
    aff = GsA:CsF:kF3 + GsA:CsF:kEnumF3 + GsA:CsF:g_f3;
    ff = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
    aff = GsA:CsF:kF4 + GsA:CsF:kEnumF4 + GsA:CsF:g_f4;
    GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
    GsA:GsG:intG agg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
    gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
    agg = GsA:GsG:kG2 + GsA:GsG:kEnumG2 + GsA:GsG:g_g2;
    GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
    GsA:GsH:intH ahh = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
    /* access base scope things qualifiers optional. */
    /* CsB has no base. */
    __println("CsB:m_b4");
}

/* inline reopen derived class. */
void CsD:m_d4() {
    /* access scope things qualifiers optional. */
    intD d = d_ + kD1 + kEnumD1 + g_d1;
    CsD:intD cd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
    GsA:CsD:intD acd = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
    d = kD2 + kEnumD2 + g_d2;
    cd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
    acd = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
    /* access enclosing scope things qualifier optional. */
    intA a = kA1 + kEnumA1 + g_a1;
    GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
    a = kA2 + kEnumA2 + g_a2;
    aa = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
    /* access sibling scope things with qualifiers. */
    CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
    GsA:CsB:intB abb = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
    bb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
    abb = GsA:CsB:kB2 + GsA:CsB:kEnumB2 + GsA:CsB:g_b2;
    CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
    GsA:CsE:intE aee = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
    ee = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
    aee = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
    CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
    GsA:CsF:intF aff = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
    ff = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
    aff = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
    ff = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
    aff = GsA:CsF:kF3 + GsA:CsF:kEnumF3 + GsA:CsF:g_f3;
    ff = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
    aff = GsA:CsF:kF4 + GsA:CsF:kEnumF4 + GsA:CsF:g_f4;
    GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
    GsA:GsG:intG agg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
    gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
    agg = GsA:GsG:kG2 + GsA:GsG:kEnumG2 + GsA:GsG:g_g2;
    GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
    GsA:GsH:intH ahh = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
    /* access base scope things qualifiers optional. */
    intC c = kC1 + kEnumC1 + g_c1;
    GsC:intC cc = GsC:kC1 + GsC:kEnumC1 + GsC:g_c1;
    c = kC2 + kEnumC2 + g_c2;
    cc = GsC:kC2 + GsC:kEnumC2 + GsC:g_c2;
    __println("CsD:m_d4");
}

/* inline reopen derived class. */
void GsA:CsE:m_e4() {
    /* access scope things qualifiers optional. */
    intE e = e_ + kE1 + kEnumE1 + g_e1;
    CsE:intE ce = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
    GsA:CsE:intE ace = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
    e = kE2 + kEnumE2 + g_e2;
    ce = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
    ace = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
    /* access enclosing scope things qualifier optional. */
    intA a = kA1 + kEnumA1 + g_a1;
    GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
    a = kA2 + kEnumA2 + g_a2;
    aa = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
    /* access sibling scope things with qualifiers. */
    CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
    GsA:CsB:intB abb = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
    bb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
    abb = GsA:CsB:kB2 + GsA:CsB:kEnumB2 + GsA:CsB:g_b2;
    CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
    GsA:CsD:intD add = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
    dd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
    add = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
    CsF:intF ff = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
    GsA:CsF:intF aff = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
    ff = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
    aff = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
    ff = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
    aff = GsA:CsF:kF3 + GsA:CsF:kEnumF3 + GsA:CsF:g_f3;
    ff = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
    aff = GsA:CsF:kF4 + GsA:CsF:kEnumF4 + GsA:CsF:g_f4;
    GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
    GsA:GsG:intG agg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
    gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
    agg = GsA:GsG:kG2 + GsA:GsG:kEnumG2 + GsA:GsG:g_g2;
    GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
    GsA:GsH:intH ahh = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
    __println("CsE:m_e4");
}

/* inline reopen incomplete class. */
void GsA:CsF:m_f6() {
    /* access scope things qualifiers optional. */
    intF f = f_ + kF1 + kEnumF1 + g_f1;
    CsF:intF cf = CsF:kF1 + CsF:kEnumF1 + CsF:g_f1;
    GsA:CsF:intF acf = GsA:CsF:kF1 + GsA:CsF:kEnumF1 + GsA:CsF:g_f1;
    f = kF2 + kEnumF2 + g_f2;
    cf = CsF:kF2 + CsF:kEnumF2 + CsF:g_f2;
    acf = GsA:CsF:kF2 + GsA:CsF:kEnumF2 + GsA:CsF:g_f2;
    f = kF3 + kEnumF3 + g_f3;
    cf = CsF:kF3 + CsF:kEnumF3 + CsF:g_f3;
    acf = GsA:CsF:kF3 + GsA:CsF:kEnumF3 + GsA:CsF:g_f3;
    f = kF4 + kEnumF4 + g_f4;
    cf = CsF:kF4 + CsF:kEnumF4 + CsF:g_f4;
    acf = GsA:CsF:kF4 + GsA:CsF:kEnumF4 + GsA:CsF:g_f4;
    /* access enclosing scope things qualifier optional. */
    intA a = kA1 + kEnumA1 + g_a1;
    GsA:intA aa = GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
    a = kA2 + kEnumA2 + g_a2;
    aa = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
    /* access sibling scope things with qualifiers. */
    CsB:intB bb = CsB:kB1 + CsB:kEnumB1 + CsB:g_b1;
    GsA:CsB:intB abb = GsA:CsB:kB1 + GsA:CsB:kEnumB1 + GsA:CsB:g_b1;
    bb = CsB:kB2 + CsB:kEnumB2 + CsB:g_b2;
    abb = GsA:CsB:kB2 + GsA:CsB:kEnumB2 + GsA:CsB:g_b2;
    CsD:intD dd = CsD:kD1 + CsD:kEnumD1 + CsD:g_d1;
    GsA:CsD:intD add = GsA:CsD:kD1 + GsA:CsD:kEnumD1 + GsA:CsD:g_d1;
    dd = CsD:kD2 + CsD:kEnumD2 + CsD:g_d2;
    add = GsA:CsD:kD2 + GsA:CsD:kEnumD2 + GsA:CsD:g_d2;
    CsE:intE ee = CsE:kE1 + CsE:kEnumE1 + CsE:g_e1;
    GsA:CsE:intE aee = GsA:CsE:kE1 + GsA:CsE:kEnumE1 + GsA:CsE:g_e1;
    ee = CsE:kE2 + CsE:kEnumE2 + CsE:g_e2;
    aee = GsA:CsE:kE2 + GsA:CsE:kEnumE2 + GsA:CsE:g_e2;
    GsG:intG gg = GsG:kG1 + GsG:kEnumG1 + GsG:g_g1;
    GsA:GsG:intG agg = GsA:GsG:kG1 + GsA:GsG:kEnumG1 + GsA:GsG:g_g1;
    gg = GsG:kG2 + GsG:kEnumG2 + GsG:g_g2;
    agg = GsA:GsG:kG2 + GsA:GsG:kEnumG2 + GsA:GsG:g_g2;
    GsH:intH hh = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
    GsA:GsH:intH ahh = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
    __println("CsF:m_f6");
}

/* block-mode reopen of the new class at file scope. */
GsH() {
    /* define more things. */
    const int kH3 = 49;
    enum EnumH3 ( kEnumH3 );
    global int g_h3 = 50;

    void m_h4() {
        /* access scope things qualifiers optional. */
        intH h = h_ + kH1 + kEnumH1 + g_h1;
        GsH:intH ch = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
        GsA:GsH:intH ach = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
        h = kH2 + kEnumH2 + g_h2;
        ch = GsH:kH2 + GsH:kEnumH2 + GsH:g_h2;
        ach = GsA:GsH:kH2 + GsA:GsH:kEnumH2 + GsA:GsH:g_h2;
        h = kH3 + kEnumH3 + g_h3;
        ch = GsH:kH3 + GsH:kEnumH3 + GsH:g_h3;
        ach = GsA:GsH:kH3 + GsA:GsH:kEnumH3 + GsA:GsH:g_h3;
        __println("GsH:m_h4");
    }
}

/* inline-mode reopen of the new class at file scope. */
void GsA:GsH:m_h5() {
    /* access scope things qualifiers optional. */
    intH h = h_ + kH1 + kEnumH1 + g_h1;
    GsH:intH ch = GsH:kH1 + GsH:kEnumH1 + GsH:g_h1;
    GsA:GsH:intH ach = GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
    h = kH2 + kEnumH2 + g_h2;
    ch = GsH:kH2 + GsH:kEnumH2 + GsH:g_h2;
    ach = GsA:GsH:kH2 + GsA:GsH:kEnumH2 + GsA:GsH:g_h2;
    h = kH3 + kEnumH3 + g_h3;
    ch = GsH:kH3 + GsH:kEnumH3 + GsH:g_h3;
    ach = GsA:GsH:kH3 + GsA:GsH:kEnumH3 + GsA:GsH:g_h3;
    __println("GsH:m_h5");
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

/* negatives: file-scope class with new fields after the class is already complete. */
//-EXPECT-ERROR: is already complete
//GsA(int a_ = 5) { }
//-EXPECT-ERROR: is already complete
//GsC(int c_ = 53) { }

int32 main() {
    {
        GsA gsa;
        h = gsa.a_ + GsA:kA1 + GsA:kEnumA1 + GsA:g_a1;
        h = GsA:kA2 + GsA:kEnumA2 + GsA:g_a2;
        gsa.m_a1();
        gsa.m_a2();
        gsa.m_a3();
        gsa.m_a4();

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
        csd.m_c2();

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

        GsA:GsH gsh;
        h = gsh.h_ + GsA:GsH:kH1 + GsA:GsH:kEnumH1 + GsA:GsH:g_h1;
        gsh.m_h1();
        gsh.m_h2();
        gsh.m_h3();
        gsh.m_h4();
        gsh.m_h5();

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
    //-EXPECT-ERROR: Undefined variable: kA2
    //h = kA2;
    //-EXPECT-ERROR: Undefined variable: kEnumA2
    //h = kEnumA2;
    //-EXPECT-ERROR: Undefined variable: g_a2
    //h = g_a2;
    //-EXPECT-ERROR: Undefined variable: kA3
    //h = kA3;
    //-EXPECT-ERROR: Undefined variable: kEnumA3
    //h = kEnumA3;
    //-EXPECT-ERROR: Undefined variable: g_a3
    //h = g_a3;
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
    //-EXPECT-ERROR: Undefined variable: kH3
    //h = kH3;
    //-EXPECT-ERROR: Undefined variable: kEnumH3
    //h = kEnumH3;
    //-EXPECT-ERROR: Undefined variable: g_h3
    //h = g_h3;

    return 0;
}
