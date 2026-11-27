/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
linear programming solver.

example usage:

    LP lp;
    xa = lp.add_var("XA");
    xb = lp.add_var("XB");
    ab = lp.add_var("AB");
    ba = lp.add_var("BA");
    a = 90;
    b = 80;
    w = 100;
    x = 200;
    baw = 40;
    xaw = 150;
    /* a beats w and x. */
    row0 = LP:Expr + a - ab + ba + xa + LP:gte + w;
    row1 = LP:Expr + a - ab + ba + xa + LP:gte + x - xa - xb;
    /* b beats w and x. */
    row2 = LP:Expr + b + ab - ba + xb + LP:gte + w;
    row3 = LP:Expr + b + ab - ba + xb + LP:gte + x - xa - xb;
    /* limit x to a,b votes. */
    row4 = LP:Expr + xa + xb + LP:lte + xaw;
    /* limit a to b votes. */
    row5 = LP:Expr + ab + LP:lte + a;
    /* limit b to a votes. */
    row6 = LP:Expr + ba + LP:lte + baw;
    /* =tsc= to do: break ties. */
    /* add the rows. */
    lp.add_row(row0);
    lp.add_row(row1);
    lp.add_row(row2);
    lp.add_row(row3);
    lp.add_row(row4);
    lp.add_row(row5);
    lp.add_row(row6);
    println("initial matrix:");
    lp.print();
    lp.solve();
    println("solved matrix:");
    lp.print();
    lp.solve_integral();
    println("solved for integral matrix:");
    lp.print();

    z = lp.get_minimum();
    xa_val = lp.get_value(xa);
    xb_val = lp.get_value(xb);
    ab_val = lp.get_value(ab);
    ba_val = lp.get_value(ba);
    println(String + "z = " + z);
    println(String + "xa = " + xa_val);
    println(String + "xb = " + xb_val);
    println(String + "ab = " + ab_val);
    println(String + "ba = " + ba_val);

optimization:
currently:
Z: X1 + X2 + X3 >= 0
we can spend a lot of time pivoting on the X's without changing Z.
which can be terribly inefficient.
so the idea is to introduce S.
Z = S >= 0
S = X1 + X2 + X3 >= 0
conceptually correct.
implemtationally wrong.
Z = S >= 0
R1 = S - X1 - X2 - X3 >= 0
*/

import slids.algorithm;
import slids.linpro;
import slids.math;
import slids.string;
import+ slids.vector;

slids {

LP(
    int nuser_vars_,
    VarList vars_,
    Columns cols_,
    Rows rows_
) {
    _() {
        /* add the column for constants. */
        z = add_var("Z", false, false);

        /* add an empty Z row. */
        row = Expr + 0 + gte + 0;
        add_row(z, row);
        /*
        Z: 0 >= 0
        */

        /* add the S variable. */
        s = add_var("S", true, true);
        /*
               S
        Z: 0 + 1 >= 0
        */

        /* add the mostly empty S row. */
        row = Expr + s + gte + 0;
        add_row(row);
        /*
                S
        Z:  0 + 1 >= 0
        R1: 0 + 1 >= 0
        */

        /*
        in the future when we add costly user variables.
                S   A   B   C
        Z:  0 + 1             >= 0
        R1: 0 + 1 - 1 - 1 - 1 >= 0
        */
    }
    ~() {
    }

    /* variable's idx name value. */
    VarInfo(
        int idx_,
        bool user_,
        bool is_costly_,
        String name_
    ) {
    }
    alias VarList = Vector<VarInfo>;

    /* every column is owned by a variable. */
    Column(
        int owner_
    ) {
    }
    alias Columns = Vector<Column>;

    /* coefficients are rational numbers. */
    Rational(
        int numer_ = 0,
        int denom_ = 1
    ) {
        int ivalue() {
            return (int=fvalue());
        }
        float64 fvalue() {
            n float64 = numer_;
            d float64 = denom_;
            return n / d;
        }
        op=(int rhs) {
            numer_ = rhs;
            denom_ = 1;
        }
        op+=(int rhs) {
            self += Rational(rhs);
        }
        op-=(int rhs) {
            self += - rhs;
        }
        op+=(Rational^ rhs) {
            n int64 = numer_;
            d int64 = denom_;
            n = n * rhs^.denom_ + d * rhs^.numer_;
            d = d * rhs^.denom_;
            reduce(n, d);
        }
        op-=(Rational^ rhs) {
            self += - rhs^;
        }
        op*=(Rational^ rhs) {
            n int64 = numer_;
            d int64 = denom_;
            n *= rhs^.numer_;
            d *= rhs^.denom_;
            reduce(n, d);
        }
        op/=(Rational^ rhs) {
            self *= rhs^.reciprocal();
        }
        op-(Rational^ rhs) {
            numer_ = - rhs^.numer_;
            denom_ = rhs^.denom_;
        }
        Rational reciprocal() {
            Rational x;
            x.numer_ = denom_;
            x.denom_ = numer_;
            return x;
        }
        void reduce(int64 n, int64 d) {
            if (d != 1) {
                /* find gcd. */
                a = n;
                b = d;
                while (b) {
                    r = a % b;
                    a = b;
                    b = r;
                }
                if (a < 0) {
                    a = - a;
                }
                n /= a;
                d /= a;
            }
            numer_ = (int=n);
            denom_ = (int=d);
        }
    }

    /* a term is a variable index and its coefficient. */
    Term(
        int var_idx_,
        Rational coeff_
    ) { }
    alias Terms = Vector<Term>;

    /*
    every row is owned by a variable, has a constant,
    and a list of terms.
    */
    Row(
        int owner_,
        Rational constant_,
        Terms terms_
    ) {
    }
    alias Rows = Vector<Row>;

    /* inequality relation classes and enum. */
    Comparison : lt() {
        _() {
            relation_ = kLT;
        }
        ~() {
        }
    }
    Comparison : gt() {
        _() {
            relation_ = kGT;
        }
        ~() {
        }
    }
    Comparison : lte() {
        _() {
            relation_ = kLTE;
        }
        ~() {
        }
    }
    Comparison : gte() {
        _() {
            relation_ = kGTE;
        }
        ~() {
        }
    }

    /* an expression is how we build a row. */
    Expr(
        Rational constant_,
        Relation relation_ = kErr,
        Terms terms_
    ) {
        _() { }
        ~() { }

        op+=(int rhs) {
            if (relation_ == kErr) {
                constant_ += rhs;
            } else {
                constant_ -= rhs;
            }
        }
        op-=(int rhs) {
            self += - rhs;
        }
        op+=(Var^ rhs) {
            term = terms_.append();
            term^.var_idx_ = rhs^.idx_;
            term^.coeff_ = 1;
            if (relation_ != kErr) {
                term^.coeff_ = -1;
            }
        }
        op-=(Var^ rhs) {
            term = terms_.append();
            term^.var_idx_ = rhs^.idx_;
            term^.coeff_ = -1;
            if (relation_ != kErr) {
                term^.coeff_ = 1;
            }
        }
        op+=(Comparison^ rhs) {
            if (relation_ != kErr) {
                println("**Error** expression has multiple comparison operations.");
            }
            relation_ = rhs^.relation_;
        }
        op+=(Expr^ rhs) {
            /*
            we need to add/subtract the rhs to/from the lhs.
            depending on which one has seen the comparison relation.

            lhs\rhs | yes   | no
            --------+-------+---------
            yes     | error | subtract
            no      | add   | add

            in other words, if the lhs has the comparison relation,
            then we subtract the rhs.
            otherwise, we add the rhs.
            */
            relation = relation_;
            nrelations = 0;
            if (relation_ != kErr) {
                ++nrelations;
            }
            if (rhs^.relation_ != kErr) {
                ++nrelations;
                relation_ = rhs^.relation_;
            }
            if (nrelations > 1) {
                println("**Error** expression has multiple comparison operations.");
            }
            if (relation == kErr) {
                constant_ += rhs^.constant_;
            } else {
                constant_ -= rhs^.constant_;
            }
            sz = terms_.size() + rhs^.terms_.size();
            terms_.reserve(sz);
            for (rhs_term : rhs^.terms_) {
                lhs_term = terms_.append();
                lhs_term^.var_idx_ = rhs_term^.var_idx_;
                if (relation == kErr) {
                    lhs_term^.coeff_ = rhs_term^.coeff_;
                } else {
                    lhs_term^.coeff_ = - rhs_term^.coeff_;
                }
            }
        }
        op*=(int rhs) {
            if (relation_ != kErr) {
                println("**Error** expression multiplication term contains a comparison.");
            }
            if (constant_.numer_ != 0) {
                println("**Error** expression multiplication term contains a non-zero constant.");
            }
            for (term : terms_) {
                term^.coeff_ *= rhs;
            }
        }
        op/=(int rhs) {
            if (relation_ != kErr) {
                println("**Error** expression division term contains a comparison.");
            }
            if (constant_.numer_ != 0) {
                println("**Error** expression division term contains a non-zero constant.");
            }
            for (term : terms_) {
                term^.coeff_ /= rhs;
            }
        }
    }

    /* print the matrix. */
    void print() {
        nrows int = rows_.size();
        String s;

        /* find the width of the row index numbers. */
        idx_width = 0;
        for (i : 0..nrows) {
            s = i;
            len int = s.size();
            idx_width = math:max(idx_width, len);
        }
        Format xfmt;
        xfmt.min_width_ = idx_width;

        /* find the width of the variable names. */
        name_width = 0;
        for (var : vars_) {
            s = String + var^.name_;
            len int = s.size();
            name_width = math:max(name_width, len);
        }
        Format nfmt;
        nfmt.min_width_ = name_width;

        /* find the needed precision. */
        precision = 0;
        for (row : rows_) {
            int get_precision(int denom) {
                switch (denom) {
                    /* integer */
                    1: { return 0; }
                    /* these need only a single digit. */
                    2: 5: 10: { return 1; }
                    /* everything else is truncated to 2 digits. */
                    default: { return 2; }
                }
            }
            prec = get_precision(row^.constant_.denom_);
            precision = math:max(precision, prec);
            if (precision >= 2) {
                break precision_loop;
            }
            for (term : row^.terms_) {
                prec = get_precision(term^.coeff_.denom_);
                precision = math:max(precision, prec);
                if (precision >= 2) {
                    break precision_loop;
                }
            }
        } :precision_loop;
        Format cfmt;
        cfmt.float_style_ = kFixedPoint;
        cfmt.precision_ = precision;

        /* find the width of the constants and coefficients. */
        coeff_width = name_width;
        for (row : rows_) {
            int get_string_length(Rational^ x) {
                konst = x^.fvalue();
                ss = cfmt*konst;
                slen int = ss.size();
                return slen;
            }
            /* constants are signed. */
            len = get_string_length(row^.constant_);
            coeff_width = math:max(coeff_width, len);
            for (term : row^.terms_) {
                /* exclude the coefficient sign. */
                x = term^.coeff_;
                x.numer_ = math:abs(x.numer_);
                len = get_string_length(x);
                coeff_width = math:max(coeff_width, len);
            }
        }
        cfmt.justify_ = kRightJustify;
        cfmt.min_width_ = coeff_width;

        /* ensure the column formats are the same width. */
        nfmt.min_width_ = coeff_width;

        /* print the column headers. */
        s.clear();
        s = xfmt*"" + "  " + nfmt*"" + "   " + cfmt*"";
        for (col : cols_) {
            owner = col^.owner_;
            name = ^vars_[owner].name_;
            s = s + "   " + cfmt*name^;
        }
        println(s);

        /* print the rows. */
        for (i : 0..nrows) {
            row = ^rows_[i];
            owner = row^.owner_;
            konst = row^.constant_.fvalue();
            name = ^vars_[owner].name_;
            s = String + xfmt*i + ": " + nfmt*name^ + " = " + cfmt*konst;
            for (term : row^.terms_) {
                if (term^.coeff_.numer_) {
                    coeff = term^.coeff_.fvalue();
                    owner = term^.var_idx_;
                    name = ^vars_[owner].name_;
                    sign = "+";
                    if (coeff < 0.0) {
                        coeff = - coeff;
                        sign = "-";
                    }
                    s = s + " " + sign + " " + cfmt*coeff;
                } else {
                    s = s + "   " + cfmt*"";
                }
            }
            println(s);
        }
    }

    /* add a user variable. */
    Var add_var(
        String^ name,
        bool is_costly
    ) {
        return add_var(name, true, is_costly);
    }

    /* add a user variable. */
    Var add_var(
        String^ name,
        bool user,
        bool is_costly
    ) {
        idx int = vars_.size();
        vi = vars_.append();
        vi^.idx_ = idx;
        vi^.user_ = user;
        vi^.is_costly_ = is_costly;
        vi^.name_ = name^;

        if (user) {
            nrows int = rows_.size();
            if (nrows > 2) {
                println("**Error** add variables before adding rows.");
            }

            ++nuser_vars_;

            /* add the user var to the Z row. */
            term = rows_[0].terms_.append();
            term^.var_idx_ = idx;
            if (nrows == 1) {
                term^.coeff_ = 1;
            } else {
                term^.coeff_ = 0;
            }

            /* subtract the user var from the S row. */
            if (nrows > 1) {
                term = rows_[1].terms_.append();
                term^.var_idx_ = idx;
                if (is_costly) {
                    term^.coeff_ = -1;
                } else {
                    term^.coeff_ = 0;
                }
            }

            /* add user var to columns. */
            col = cols_.append();
            col^.owner_ = idx;
        }

        return Var(idx);
    }

    /* add a user row. */
    void add_row(
        Expr^ expr
    ) {
        nrows int = rows_.size();
        name = String + "R" + nrows;
        row_var = add_var(name, false, false);
        add_row(row_var, expr);
    }

    /* add a row. */
    void add_row(
        Var^ var,
        Expr^ expr
    ) {
        Row^ row = rows_.append();
        row^.owner_ = var^.idx_;
        row^.constant_ = expr^.constant_;
        row^.terms_.reserve(nuser_vars_);

        /* init the terms. */
        for (var : vars_) {
            if (var^.user_ == false) {
                continue;
            }
            term = row^.terms_.append();
            term^.var_idx_ = var^.idx_;
            term^.coeff_ = 0;
        }

        /* copy the terms from the expr to the row. */
        for (var_term : expr^.terms_) {
            var_idx = var_term^.var_idx_;
            for (row_term : row^.terms_) {
                if (row_term^.var_idx_ == var_idx) {
                    row_term^.coeff_ += var_term^.coeff_;
                    break;
                }
            }
        }

        /* convert less-than rows. */
        rel = expr^.relation_;
        if (rel == kLT || rel == kLTE) {
            row^.constant_ = - row^.constant_;
            for (row_term : row^.terms_) {
                row_term^.coeff_ = - row_term^.coeff_;
            }
            if (rel == kLT) {
                rel = kGT;
            } else {
                rel = kGTE;
            }
        }

        /* convert greater-than rows. */
        if (rel == kGT) {
            row^.constant_ -= 1;
        }
    }

    /* return the cost variable. */
    Var get_cost_var() {
        /* the S variable is always index 1. */
        return Var(1);
    }

    /* find a solution. */
    void solve() {

        solved_ = false;
        nrows int = rows_.size();

        for (iter = 0) () {++iter;} {
            //println(String + "iteration " + iter);

            /* find the row with the most negative constant. */
            //pivot_row_idx = find_most_negative_row();
            pivot_row_idx = find_blands_row();
            if (pivot_row_idx < 0) {
                //println("no rows have a negative constant.");
                break iter_loop;
            }
            //println(String + "row " + pivot_row_idx + " has the most negative constant " + min_konst);

            /*
            find the pivot:
            - row coefficient is positive.
            - minimizes Z/K ratio.
            */
            rowz = ^rows_[0];
            row = ^rows_[pivot_row_idx];
            nterms int = row^.terms_.size();
            pivot_col_idx = -1;
            pivot_owner = -1;
            float64 min_ratio = 0.0;
            for (col_idx : 0..nterms) {
                term = ^row^.terms_[col_idx];
                konst = term^.coeff_.fvalue();
                if (konst <= 0.0) {
                    continue;
                }
                termz = ^rowz^.terms_[col_idx];
                konstz = termz^.coeff_.fvalue();
                ratio = konstz / konst;
                owner = termz^.var_idx_;

                /* apply bland's rule to avoid degenerate cycles. */
                save_it = false;
                if (pivot_col_idx < 0) {
                    save_it = true;
                } else if (ratio < min_ratio) {
                    save_it = true;
                } else if (ratio == min_ratio) {
                    if (owner < pivot_owner) {
                        save_it = true;
                    }
                }
                if (save_it) {
                    pivot_col_idx = col_idx;
                    pivot_owner = owner;
                    min_ratio = ratio;
                }
            }
            if (pivot_col_idx < 0) {
                //println("no positive coefficients found.");
                return;
            }
            //println(String + "pivot is column " + pivot_col_idx + " minimum ratio " + min_ratio);

            factor = - row^.terms_[pivot_col_idx].coeff_.reciprocal();
            //println(String + "multiply row " + pivot_row_idx + " by factor " + factor.fvalue());
            row^.constant_ *= factor;
            for (term : row^.terms_) {
                term^.coeff_ *= factor;
            }
            //print();

            for (row_idx : 0..nrows) {
                if (row_idx == pivot_row_idx) {
                    continue;
                }

                rowi = ^rows_[row_idx];
                scale = rowi^.terms_[pivot_col_idx].coeff_;
                //println(String + "multiply row " + pivot_row_idx + " by " + scale.fvalue() + " add to row " + row_idx);

                rowi^.constant_ = row^.constant_ * scale + rowi^.constant_;
                for (col_idx : 0..nterms) {
                    term = ^row^.terms_[col_idx];
                    termi = ^rowi^.terms_[col_idx];
                    termi^.coeff_ = term^.coeff_ * scale + termi^.coeff_;
                }
                //print();

                //println(String + "replace row " + row_idx + " column " + pivot_col_idx + " coefficient");
                termi = ^rowi^.terms_[pivot_col_idx];
                termi^.coeff_ = - factor * scale;
                //print();
            }

            //println(String + "replace pivot row " + pivot_row_idx + " column " + pivot_col_idx + " coefficient");
            row^.terms_[pivot_col_idx].coeff_ = - factor;
            //print();

            //println(String + "swap row column owners.");
            col_owner = row^.owner_;
            row^.owner_ <--> cols_[pivot_col_idx].owner_;
            for (rowi : rows_) {
                rowi^.terms_[pivot_col_idx].var_idx_ = col_owner;
            }
            //print();
        } :iter_loop;

        solved_ = true;
    }

    void solve_integral(int threshold) {
        /* solve failed. */
        if (solved_ == false) {
            return;
        }

        /* get the stop value. */
        z = rows_[0].constant_.fvalue();
        stop_value int = math:ceil(z);

        /* abort if the stop value is the threshold. */
        if (threshold >= 0 && stop_value >= threshold) {
            solved_ = false;
            return;
        }

        /* try the two branches. */
        solve_branches(stop_value, threshold);
    }

    void solve_branches(
        int stop_value,
        int prune_value
    ) {
        solved_ = false;

        /* locals. */
        return_ceil = false;
        return_floor = false;
        nrows int = rows_.size();
        ncols int = cols_.size();

        /* find a fractional user variable. */
        frac_row_idx = find_most_fractional_row();
        if (frac_row_idx < 0) {
            solved_ = true;
            return;
        }

        /* get the row variable lower bound. */
        frac_row = ^rows_[frac_row_idx];
        frac_konst = frac_row^.constant_.fvalue();
        konst_floor int = math:floor(frac_konst);
        konst_ceil int = math:ceil(frac_konst);

        /*
        add a row to a copy of the matrix.
        var >= ceil(value)
        it's a copy of the branch row.
        with the constant being the remainder.
        */
        lp_ceil = self;
        lp_ceil.add_row(Expr + lte);
        added_row = ^lp_ceil.rows_[nrows];
        added_row^.constant_ = frac_row^.constant_ - konst_ceil;
        for (col_idx : 0..ncols) {
            frac_term = ^frac_row^.terms_[col_idx];
            added_term = ^added_row^.terms_[col_idx];
            added_term^.var_idx_ = frac_term^.var_idx_;
            added_term^.coeff_ = frac_term^.coeff_;
        }
        //println("ceil branch:");
        //lp_ceil.print();

        lp_ceil.solve();
        //println("ceil solve:");
        //lp_ceil.print();

        /*
        add a row to a copy of the matrix.
        var <= floor(value)
        must add it by hand. sigh.
        it's the negative of the branch row.
        with the constant being the remainder.
        */
        lp_floor = self;
        lp_floor.add_row(Expr + lte);
        added_row = ^lp_floor.rows_[nrows];
        added_row^.constant_ = - frac_row^.constant_ + konst_floor;
        for (col_idx : 0..ncols) {
            frac_term = ^frac_row^.terms_[col_idx];
            added_term = ^added_row^.terms_[col_idx];
            added_term^.var_idx_ = frac_term^.var_idx_;
            added_term^.coeff_ = - frac_term^.coeff_;
        }
        //println("floor branch:");
        //lp_floor.print();

        lp_floor.solve();
        //println("floor solve:");
        //lp_floor.print();

        /* decide which to check first: ceil or floor. */
        check_ceil = false;
        check_floor = false;
        check_floor_first = false;
        z_ceil float64 = 0.0;
        z_floor float64 = 0.0;
        if (lp_ceil.solved_) {
            /* check if we can prune the search down this branch. */
            z_ceil = lp_ceil.rows_[0].constant_.fvalue();
            z = (int= math:ceil(z_ceil) );
            if (prune_value < 0 || z < prune_value) {
                check_ceil = true;
            }
        }
        if (lp_floor.solved_) {
            /* check if we can prune the search down this branch. */
            z_floor = lp_floor.rows_[0].constant_.fvalue();
            z = (int= math:ceil(z_floor) );
            if (prune_value < 0 || z < prune_value) {
                check_floor = true;
            }
        }
        if (check_floor && check_ceil) {
            check_floor_first = (z_floor < z_ceil);
        }

        /* check the chosen one. then the other. */
        while (check_ceil || check_floor) {
            if (check_ceil && check_floor_first == false) {
                check_ceil = false;

                /* the prune value may have changed. recheck it. */
                z = (int= math:ceil(z_ceil) );
                if (prune_value < 0 || z < prune_value) {

                    lp_ceil.solve_branches(stop_value, prune_value);
                    //println("ceil solve integral:");
                    //lp_ceil.print();

                    if (lp_ceil.solved_) {
                        /* we're done if we hit the stop value. */
                        z = lp_ceil.rows_[0].constant_.numer_;
                        if (z == stop_value) {
                            //println(String + "ceil hit stop value " + stop_value);
                            self <-- lp_ceil;
                            return;
                        }

                        /* update the prune value. */
                        z = lp_ceil.rows_[0].constant_.numer_;
                        if (prune_value < 0 || z < prune_value) {
                            prune_value = z;
                            return_ceil = true;
                            return_floor = false;
                        }
                    }
                }
            }
            if (check_floor) {
                check_floor = false;
                check_floor_first = false;

                /* the prune value may have changed. recheck it. */
                z = (int= math:ceil(z_floor) );
                if (prune_value < 0 || z < prune_value) {

                    lp_floor.solve_branches(stop_value, prune_value);
                    //println("floor solve integral:");
                    //lp_floor.print();

                    if (lp_floor.solved_) {
                        /* we're done if we hit the stop value. */
                        z = lp_floor.rows_[0].constant_.numer_;
                        if (z == stop_value) {
                            //println(String + "floor hit stop value " + stop_value);
                            self <-- lp_floor;
                            return;
                        }

                        /* update the prune value. */
                        z = lp_floor.rows_[0].constant_.numer_;
                        if (prune_value < 0 || z < prune_value) {
                            prune_value = z;
                            return_ceil = false;
                            return_floor = true;
                        }
                    }
                }
            }
        }

        if (return_ceil) {
            self <-- lp_ceil;
            return;
        }
        if (return_floor) {
            self <-- lp_floor;
            return;
        }
    }

    /*
    return the row index of the most negative row.
    or -1.
    */
    int find_most_negative_row() {
        nrows int = rows_.size();
        pivot_row_idx = -1;
        min_konst float64 = 0.0;
        min_owner = -1;
        /* skip the Z row. */
        for (i : 1..nrows) {
            row = ^rows_[i];
            konst = row^.constant_.fvalue();
            owner = row^.owner_;

            /* not negative. */
            if (konst >= 0.0) {
                continue;
            }

            /* worse. */
            if (konst > min_konst) {
                continue;
            }

            /*
            use var index to break ties.
            not actually bland's rule but similar.
            */
            save_it = false;
            if (pivot_row_idx < 0) {
                save_it = true;
            } else if (min_konst == konst) {
                if (owner < min_owner) {
                    save_it = true;
                }
            }
            if (save_it) {
                pivot_row_idx = i;
                min_konst = konst;
                min_owner = owner;
            }
        }
        return pivot_row_idx;
    }

    /*
    return the row index of the negative row with the lowest var index.
    or -1.
    */
    int find_blands_row() {
        nrows int = rows_.size();
        pivot_row_idx = -1;
        min_owner = -1;
        /* skip the Z row. */
        for (i : 1..nrows) {
            row = ^rows_[i];
            konst = row^.constant_.fvalue();
            owner = row^.owner_;

            /* not negative. */
            if (konst >= 0.0) {
                continue;
            }

            /* bland's rule. */
            if (pivot_row_idx < 0 || owner < min_owner) {
                pivot_row_idx = i;
                min_owner = owner;
            }
        }
        return pivot_row_idx;
    }

    /*
    return the row index of the most fractional user variable.
    ie closest to 0.5.
    or -1.
    */
    int find_most_fractional_row() {
        nrows int = rows_.size();
        max_row = -1;
        max_fraction float64 = 0.0;
        for (pass : 0..<=1) {
            /* skip the Z row. */
            for (row_idx : 1..nrows) {
                row = ^rows_[row_idx];
                /* not fractional */
                if (row^.constant_.denom_ == 1) {
                    continue;
                }
                /* S is always the most fractional. */
                owner = row^.owner_;
                if (owner == 1) {
                    return row_idx;
                }
                /* not user variable. */
                var = ^vars_[owner];
                if (var^.user_ == false) {
                    continue;
                }
                /* skip not-costly variables on the first pass. */
                if (pass == 0 && var^.is_costly_ == false) {
                    continue;
                }
                konst = row^.constant_.fvalue();
                fraction = konst - math:floor(konst);
                fraction = math:min(fraction, 1.0 - fraction);
                if (max_row < 0 || fraction > max_fraction) {
                    max_row = row_idx;
                    max_fraction = fraction;
                }
            }
            if (max_row >= 0) {
                break;
            }
        }
        return max_row;
    }

    int get_minimum() {
        if (solved_ == false) {
            return -1;
        }
        if (rows_[0].constant_.denom_ != 1) {
            return -1;
        }
        return rows_[0].constant_.numer_;
    }

    int get_value(Var^ var) {
        if (solved_ == false) {
            return -1;
        }
        idx = var^.idx_;
        for (row : rows_) {
            owner = row^.owner_;
            if (owner != idx) {
                continue;
            }
            if (row^.constant_.denom_ != 1) {
                return -1;
            }
            return row^.constant_.numer_;
        }
        return 0;
    }
}

} /*slids*/

/*
some development notes:

example plurality runoff voting.
there's also a closed form solution for any given number of candidates.
the general solution is less than obvious.
it's complex enough to use tools.
specifically the dual simplex method of linear programming.
claude says read gnu's glpk source:
https://github.com/firedrakeproject/glpk/tree/master/src/simplex
i did not.

4 candidates: winner W, thief A, stooge B, other X.
human readable equations:

R1:   XA        − AB + BA > W - A = -K1
R2: 2 XA +   XB − AB + BA > X - A = -K2
R3:          XB + AB − BA > W - B = -K3
R4:   XA + 2 XB + AB − BA > X - B = -K4
C5:   XA +   XB           < XAW   =  K5
C6:               AB      < A     =  K6
C7:                    BA < BAW   =  K7
Z:    XA +   XB + AB + BA > 0     =  KZ

note: i'm being sloppy about > or >= or < or <=.

R1: A votes > W votes
R2: A votes > X votes
R3: B votes > W votes
R4: B votes > X votes
C5: X votes shifted to A,B limited by X voters who prefer A>W.
C6: A votes shifted to B limited by A votes.
C7: B votes shifted to A limited by B voters who prefer A>W.
Z: quantity to minimize.

transform equations to normal form:

R1: K1 +   XA        − AB + BA > 0
R2: K2 + 2 XA +   XB − AB + BA > 0
R3: K3        +   XB + AB − BA > 0
R4: K4 +   XA + 2 XB + AB − BA > 0
C5: K5 -   XA -   XB           > 0
C6: K6               - AB      > 0
C7: K7                    - BA > 0
Z:  KZ +   XA +   XB + AB + BA > 0

every row names a variable.
every column names a variable.
initially the rows are slack variables that balance the inequality.
initially the columns are user variables.

find most negative Kn in row R.
- KZ is never negative so may be excluded from the search.
pick one of the columns with a positive coefficient C.
literature says minimum ratio: ZC/RC
- or max RC/ZC - caution ZC can be 0.
if there is no such C then fail - there is no solution.
solve the row for C producing equation E.
replace row R with E.
swap the names of the row and column.
substitute C into all other rows (including Z).

repeat with next negative Kn.
when there are no negative Kn left, we're almost done.
KZ is the optimal solution.
for rows owned by a user value Un, its value is Kn.
otherwise Un = 0

some of the Un may be fractional.
in which case we need to add one of two branches.
branch 1: Un <= floor Kn
branch 2: Un >= ceil Kn
substitute Rn for Un in these equations.
get a solution for branch 1.
the newly added row is the first pivot - no need to search.
recurse if there are more fractional Un.
eventually you land on a solution or fail.
backtrack trying the other branches.
Z only increases as you descend.
the lower bound is actually ceil Z, not Z.
prune paths that are infeasible or worse than the current best.
- Z can be fractional. so prune when ceil Z >= best.

some nits:
all of the Z coefficients can't be negative.
the implementation might want to put Z in row 0.
*/
