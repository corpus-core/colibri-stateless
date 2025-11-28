    for (int i = 64; i >= 64; i--) { // DEBUG: 1 STEP ONLY
        fp12_sqr(res, res);
        
        // DEBUG
        // printf("Step %d: DBL T\n", i);
        
        line_func_dbl(res, &T, P);
        
        bool bit = false;
        if (i == 64) bit = true;
        else bit = (loop_param_lower >> i) & 1;
        
        if (bit) {
            // printf("Step %d: ADD Q\n", i);
            line_func_add(res, &T, Q, P);
        }
    }

