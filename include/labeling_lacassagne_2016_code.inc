public:
    void PerformLabeling() {
        int rows = img_.rows;
        int cols = img_.cols;

        // Step 1
        Mat1i ER(rows, cols);   // Matrix of relative label (1 label/pixel) 
        Mat1i RLC(rows, (cols + 1) & ~1); // MISSING in the paper: RLC requires 2 values/run in row, so width must be next multiple of 2
        int *ner = new int[rows]; //vector<int> ner(rows); // Number of runs 

        for (int r = 0; r < rows; ++r) {
            // Get pointers to rows
            const unsigned char* img_r = img_.ptr<unsigned char>(r);
            unsigned* ER_r = ER.ptr<unsigned>(r);
            unsigned* RLC_r = RLC.ptr<unsigned>(r);
            int x0;
            int x1 = 0; // Previous value of X
            int f = 0;  // Front detection
            ZOA_MOD0
            int er = 0;
            for (int c = 0; c < cols; ++c) {
                x0 = img_r[c] > 0;
                f = x0 ^ x1;
				RLE_MOD1 {
					RLC_r[er] = c ZOA_MOD1;
					ZOA_MOD2
					er = er + RLE_MOD2;
				}
                ER_r[c] = er;
                x1 = x0;
            }
            ZOA_MOD3
            RLC_r[er] = cols ZOA_MOD1;
            ZOA_MOD4
            ner[r] = er;
        }

        // Step 2
        Mat1i ERA(rows, cols + 1, 0); // MISSING in the paper: ERA must have one column more than the input image 
                                      // in order to handle special cases (e.g. lines with chessboard pattern 
                                      // starting with a foreground pixel) 

        LabelsSolver::Alloc(UPPER_BOUND_8_CONNECTIVITY);
        LabelsSolver::Setup();

        // First row
        {
            unsigned* ERA_r = ERA.ptr<unsigned>(0);
            for (int er = 1; er <= ner[0]; er += 2) {
                ERA_r[er] = LabelsSolver::NewLabel();
            }
        }
        for (int r = 1; r < rows; ++r) {
            // Get pointers to rows
            unsigned* ERA_r = ERA.ptr<unsigned>(r);
            const unsigned* ERA_r_prev = (unsigned *)(((char *)ERA_r) - ERA.step.p[0]);
            const unsigned* ER_r_prev = ER.ptr<unsigned>(r - 1);
            const unsigned* RLC_r = RLC.ptr<unsigned>(r);
            for (int er = 1; er <= ner[r]; er += 2) {
                int j0 = RLC_r[er - 1];
                int j1 = RLC_r[er] ZOA_MOD5;
                // Check extension in case of 8-connect algorithm
                if (j0 > 0)
                    j0--;
                if (j1 < cols - 1) // WRONG in the paper! "n-1" should be "w-1"
                    j1++;
                int er0 = ER_r_prev[j0];
                int er1 = ER_r_prev[j1];
                // Check label parity: segments are odd
                if (er0 % 2 == 0)
                    er0++;
                if (er1 % 2 == 0)
                    er1--;
                if (er1 >= er0) {
                    int ea = ERA_r_prev[er0];
                    int a = LabelsSolver::FindRoot(ea);
                    for (int erk = er0 + 2; erk <= er1; erk += 2) { // WRONG in the paper! missing "step 2"
                        int eak = ERA_r_prev[erk];
                        int ak = LabelsSolver::FindRoot(eak);
                        // Min extraction and propagation
                        if (a < ak)
                            LabelsSolver::UpdateTable(ak, a);
                        if (a > ak) {
                            LabelsSolver::UpdateTable(a, ak);
                            a = ak;
                        }
                    }
                    ERA_r[er] = a; // The global min
                }
                else {
                    ERA_r[er] = LabelsSolver::NewLabel();
                }
            }
        }

        // Step 3
        //Mat1i EA(rows, cols);
        //for (int r = 0; r < rows; ++r) {
        //	for (int c = 0; c < cols; ++c) {
        //		EA(r, c) = ERA(r, ER(r, c));
        //	}
        //}
        // Sorry, but we really don't get why this shouldn't be included in the last step

        // Step 4
        n_labels_ = LabelsSolver::Flatten();

        // Step 5
        img_labels_ = Mat1i(rows, cols);
        for (int r = 0; r < rows; ++r)
        {
            // Get pointers to rows
            unsigned* labels_r = img_labels_.ptr<unsigned>(r);
            const unsigned* ERA_r = ERA.ptr<unsigned>(r);
            const unsigned* ER_r = ER.ptr<unsigned>(r);
            for (int c = 0; c < cols; ++c)
            {
                //labels(r, c) = A[EA(r, c)];
                labels_r[c] = LabelsSolver::GetLabel(ERA_r[ER_r[c]]); // This is Step 3 and 5 together
            }
        }

        delete[] ner;
        LabelsSolver::Dealloc();
    }

    void PerformLabelingWithSteps()
    {
        perf_.start();
        Alloc();
        perf_.stop();
        double alloc_timing = perf_.last();

        perf_.start();
        FirstScan();
        perf_.stop();
        perf_.store(Step(StepType::FIRST_SCAN), perf_.last());

        perf_.start();
        SecondScan();
        perf_.stop();
        perf_.store(Step(StepType::SECOND_SCAN), perf_.last());

        perf_.start();
        Dealloc();
        perf_.stop();
        perf_.store(Step(StepType::ALLOC_DEALLOC), perf_.last() + alloc_timing);
    }

    void PerformLabelingMem(std::vector<unsigned long int>& accesses) {
        int rows = img_.rows;
        int cols = img_.cols;

        MemMat<int> img(img_);

        // Step 1
        MemMat<int> ER(rows, cols);   // Matrix of relative label (1 label/pixel) 
        MemMat<int> RLC(rows, (cols + 1) & ~1); // MISSING in the paper: RLC requires 2 values/run in row, so width must be next multiple of 2
        MemVector<int> ner(rows); //vector<int> ner(rows); // Number of runs 

        for (int r = 0; r < rows; ++r) {
            int x0;
            int x1 = 0; // Previous value of X
            int f = 0;  // Front detection
            int b = 0;  // Right border compensation
            int er = 0;
            for (int c = 0; c < cols; ++c)
            {
                x0 = img(r, c) > 0;
                f = x0 ^ x1;
				RLE_MOD1 {
					RLC(r, er) = c ZOA_MOD1;
					ZOA_MOD2
					er = er + RLE_MOD2;
				}
                ER(r, c) = er;
                x1 = x0;
            }
            ZOA_MOD3
			RLC(r, er) = cols ZOA_MOD1;
            ZOA_MOD4
            ner[r] = er;
        }

        // Step 2
        MemMat<int> ERA(rows, cols + 1, 0); // MISSING in the paper: ERA must have one column more than the input image 
                                            // in order to handle special cases (e.g. lines with chessboard pattern 
                                            // starting with a foreground pixel) 

        LabelsSolver::MemAlloc(UPPER_BOUND_8_CONNECTIVITY);
        LabelsSolver::MemSetup();

        // First row
        {
            for (int er = 1; er <= ner[0]; er += 2) {
                ERA(0, er) = LabelsSolver::MemNewLabel();
            }
        }
        for (int r = 1; r < rows; ++r)
        {
            for (int er = 1; er <= ner[r]; er += 2) {
                int j0 = RLC(r, er - 1);
                int j1 = RLC(r, er) ZOA_MOD5;
                // Check extension in case of 8-connect algorithm
                if (j0 > 0)
                    j0--;
                if (j1 < cols - 1) // WRONG in the paper! "n-1" should be "w-1"
                    j1++;
                int er0 = ER(r - 1, j0);
                int er1 = ER(r - 1, j1);
                // Check label parity: segments are odd
                if (er0 % 2 == 0)
                    er0++;
                if (er1 % 2 == 0)
                    er1--;
                if (er1 >= er0) {
                    int ea = ERA(r - 1, er0);
                    int a = LabelsSolver::MemFindRoot(ea);
                    for (int erk = er0 + 2; erk <= er1; erk += 2) { // WRONG in the paper! missing "step 2"
                        int eak = ERA(r - 1, erk);
                        int ak = LabelsSolver::MemFindRoot(eak);
                        // Min extraction and propagation
                        if (a < ak)
                            LabelsSolver::MemUpdateTable(ak, a);
                        if (a > ak)
                        {
                            LabelsSolver::MemUpdateTable(a, ak);
                            a = ak;
                        }
                    }
                    ERA(r, er) = a; // The global min
                }
                else
                {
                    ERA(r, er) = LabelsSolver::MemNewLabel();
                }
            }
        }

        // Step 4
        n_labels_ = LabelsSolver::MemFlatten();

        // Step 5
        MemMat<int> labels(rows, cols);
        for (int r = 0; r < rows; ++r)
        {
            for (int c = 0; c < cols; ++c)
            {
                //labels(r, c) = A[EA(r, c)];
                labels(r, c) = LabelsSolver::MemGetLabel(ERA(r, ER(r, c))); // This is Step 3 and 5 together
            }
        }

        // Store total accesses in the output vector 'accesses'
        accesses = vector<unsigned long int>((int)MD_SIZE, 0);

        accesses[MD_BINARY_MAT] = (unsigned long int)img.GetTotalAccesses();
        accesses[MD_LABELED_MAT] = (unsigned long int)labels.GetTotalAccesses();
        accesses[MD_EQUIVALENCE_VEC] = (unsigned long int)LabelsSolver::MemTotalAccesses();
        accesses[MD_OTHER] = (unsigned long int)(ER.GetTotalAccesses() + RLC.GetTotalAccesses() + ner.GetTotalAccesses() + ERA.GetTotalAccesses());

        img_labels_ = labels.GetImage();

        LabelsSolver::MemDealloc();
    }

private:
    int *ner;
    Mat1i ER, RLC, ERA;

    void Alloc() {
        LabelsSolver::Alloc(UPPER_BOUND_8_CONNECTIVITY); // Memory allocation of the labels solver
        img_labels_ = cv::Mat1i(img_.size()); // Memory allocation of the output image

        int rows = img_.rows;
        int cols = img_.cols;

        ER = Mat1i(rows, cols); // Matrix of relative label (1 label/pixel) 
        RLC = Mat1i(rows, (cols + 1) & ~1); // MISSING in the paper: RLC requires 2 values/run in row, so width must be next multiple of 2
        ner = new int[rows]; //vector<int> ner(rows); // Number of runs 

        ERA = Mat1i(rows, cols + 1, 0); // MISSING in the paper: ERA must have one column more than the input image 
                                // in order to handle special cases (e.g. lines with chessboard pattern 
                                // starting with a foreground pixel) 
    }
    void Dealloc() {
        ERA.release();

        delete[] ner;
        RLC.release();
        ER.release();

        // No free for img_labels_ because it is required at the end of the algorithm 
        LabelsSolver::Dealloc();
    }
    void FirstScan()
    {
        int rows = img_.rows;
        int cols = img_.cols;

        // Step 1
        for (int r = 0; r < rows; ++r) {
            // Get pointers to rows
            const unsigned char* img_r = img_.ptr<unsigned char>(r);
            unsigned* ER_r = ER.ptr<unsigned>(r);
            unsigned* RLC_r = RLC.ptr<unsigned>(r);
            int x0;
            int x1 = 0; // Previous value of X
            int f = 0;  // Front detection
            int b = 0;  // Right border compensation
            int er = 0;
            for (int c = 0; c < cols; ++c) {
                x0 = img_r[c] > 0;
                f = x0 ^ x1;
				RLE_MOD1 {
					RLC_r[er] = c ZOA_MOD1;
					ZOA_MOD2
					er = er + RLE_MOD2;
				}
                ER_r[c] = er;
                x1 = x0;
            }
            ZOA_MOD3
            RLC_r[er] = cols ZOA_MOD1;
            ZOA_MOD4
            ner[r] = er;
        }

        // Step 2
        LabelsSolver::Setup();

        // First row
        {
            unsigned* ERA_r = ERA.ptr<unsigned>(0);
            for (int er = 1; er <= ner[0]; er += 2) {
                ERA_r[er] = LabelsSolver::NewLabel();
            }
        }
        for (int r = 1; r < rows; ++r)
        {
            // Get pointers to rows
            unsigned* ERA_r = ERA.ptr<unsigned>(r);
            const unsigned* ERA_r_prev = (unsigned *)(((char *)ERA_r) - ERA.step.p[0]);
            const unsigned* ER_r_prev = ER.ptr<unsigned>(r - 1);
            const unsigned* RLC_r = RLC.ptr<unsigned>(r);
            for (int er = 1; er <= ner[r]; er += 2) {
                int j0 = RLC_r[er - 1];
                int j1 = RLC_r[er] ZOA_MOD5;
                // Check extension in case of 8-connect algorithm
                if (j0 > 0)
                    j0--;
                if (j1 < cols - 1) // WRONG in the paper! "n-1" should be "w-1"
                    j1++;
                int er0 = ER_r_prev[j0];
                int er1 = ER_r_prev[j1];
                // Check label parity: segments are odd
                if (er0 % 2 == 0)
                    er0++;
                if (er1 % 2 == 0)
                    er1--;
                if (er1 >= er0) {
                    int ea = ERA_r_prev[er0];
                    int a = LabelsSolver::FindRoot(ea);
                    for (int erk = er0 + 2; erk <= er1; erk += 2) { // WRONG in the paper! missing "step 2"
                        int eak = ERA_r_prev[erk];
                        int ak = LabelsSolver::FindRoot(eak);
                        // Min extraction and propagation
                        if (a < ak)
                            LabelsSolver::UpdateTable(ak, a);
                        if (a > ak)
                        {
                            LabelsSolver::UpdateTable(a, ak);
                            a = ak;
                        }
                    }
                    ERA_r[er] = a; // The global min
                }
                else
                {
                    ERA_r[er] = LabelsSolver::NewLabel();
                }
            }
        }
    }

    void SecondScan()
    {
        // Step 4
        n_labels_ = LabelsSolver::Flatten();

        // Step 5

        for (int r = 0; r < img_.rows; ++r)
        {
            // Get pointers to rows
            unsigned* labels_r = img_labels_.ptr<unsigned>(r);
            const unsigned* ERA_r = ERA.ptr<unsigned>(r);
            const unsigned* ER_r = ER.ptr<unsigned>(r);
            for (int c = 0; c < img_.cols; ++c)
            {
                //labels(r, c) = A[EA(r, c)];
                labels_r[c] = LabelsSolver::GetLabel(ERA_r[ER_r[c]]); // This is Step 3 and 5 together
            }
        }
    }