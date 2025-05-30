/*
MIT License

Copyright (C) 2025 Ryan L. Guy & Dennis Fassbaender

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/*
    This pressure solver is based on:

    A Fast Variational Framework for Accurate Solid-Fluid Coupling
     - Christopher Batty, Florence Bertails, Robert Bridson
    https://hal.archives-ouvertes.fr/file/index/docid/384725/filename/variationalFluids.pdf

    Source Code: https://github.com/christopherbatty/Fluid3D
*/
#ifndef FLUIDENGINE_PRESSURESOLVER_H
#define FLUIDENGINE_PRESSURESOLVER_H

#include "pcgsolver/sparsematrix.h"
#include "gridindexkeymap.h"
#include "gridindexvector.h"
#include "fluidmaterialgrid.h"
#include "vmath.h"
#include "fluidsimassert.h"

class MACVelocityField;
struct ValidVelocityComponentGrid;
class ParticleLevelSet;
class MeshLevelSet;

struct WeightGrid {
    Array3d<float> center;
    Array3d<float> U;
    Array3d<float> V;
    Array3d<float> W;

    WeightGrid() {}
    WeightGrid(int i, int j, int k) :
        center(i, j, k, 0.0f),
        U(i + 1, j, k, 0.0f),
        V(i, j + 1, k, 0.0f),
        W(i, j, k + 1, 0.0f) {}

    void getGridDimensions(int *i, int *j, int *k) {
        center.getGridDimensions(i, j, k);
    }

    void getCoarseGridDimensions(int *i, int *j, int *k) {
        center.getCoarseGridDimensions(i, j, k);
    }

    bool isDimensionsValidForCoarseGridGeneration() {
        return center.isDimensionsValidForCoarseGridGeneration();
    }

    void generateCoarseGrid(WeightGrid &coarseWeightGrid) {
        FLUIDSIM_ASSERT(isDimensionsValidForCoarseGridGeneration());
        FLUIDSIM_ASSERT(center.isMatchingDimensionsForCoarseGrid(coarseWeightGrid.center));

        center.generateCoarseGrid(coarseWeightGrid.center);
        U.generateCoarseFaceGridU(coarseWeightGrid.U);
        V.generateCoarseFaceGridV(coarseWeightGrid.V);
        W.generateCoarseFaceGridW(coarseWeightGrid.W);
    }

    WeightGrid generateCoarseGrid() {
        FLUIDSIM_ASSERT(isDimensionsValidForCoarseGridGeneration());

        int icoarse = 0; int jcoarse = 0; int kcoarse = 0;
        getCoarseGridDimensions(&icoarse, &jcoarse, &kcoarse);

        WeightGrid coarseWeightGrid(icoarse, jcoarse, kcoarse);
        generateCoarseGrid(coarseWeightGrid);

        return coarseWeightGrid;
    }

};


struct PressureSolverParameters {
    double cellwidth;
    double deltaTime;
    double tolerance;
    double acceptableTolerance;
    int maxIterations;
    
    MACVelocityField *velocityFieldFluid;
    MACVelocityField *velocityFieldSolid;
    ValidVelocityComponentGrid *validVelocities;
    Array3d<float> *liquidSDF;
    WeightGrid *weightGrid;
    Array3d<float> *pressureGrid;

    bool isSurfaceTensionEnabled = false;
    double surfaceTensionConstant;
    Array3d<float> *curvatureGrid;
};

/********************************************************************************
    PressureSolver
********************************************************************************/

class PressureSolver
{
public:
    PressureSolver();
    ~PressureSolver();

    bool solve(PressureSolverParameters params);
    std::string getSolverStatus();

    void applySolutionToVelocityField();

    int getMaxIterations() { return _maxCGIterations; }
    void setMaxIterations(int n) { _maxCGIterations = n; }

    int getIterations() { return _solverIterations; }
    float getError() { return _solverError; } 

private:

    inline int _GridToVectorIndex(GridIndex g) {
        return _keymap.find(g);
    }
    inline int _GridToVectorIndex(int i, int j, int k) {
        return _keymap.find(i, j, k);
    }
    inline GridIndex _VectorToGridIndex(int i) {
        return _pressureCells.at(i);
    }
    inline int _isPressureCell(GridIndex g) {
        return _keymap.find(g) != -1;
    }
    inline int _isPressureCell(int i, int j, int k) {
        return _keymap.find(i, j, k) != -1;
    }

    template <typename T>
    T _clamp(const T& n, const T& lower, const T& upper) {
      return std::max(lower, std::min(n, upper));
    }

    void _initialize(PressureSolverParameters params);
    void _initializeGridIndexKeyMap();
    void _conditionSolidVelocityField();
    void _computeBordersAirGridThread(int startidx, int endidx, 
                                      Array3d<bool> *bordersAir);

    void _initializeSurfaceTensionClusterData();
    void _initializeBlockStatusGridThread(int startidx, int endidx,
                                          Array3d<char> *blockstatus);
    void _initializeCellStatusGridThread(int startidx, int endidx,
                                         Array3d<char> *blockstatus, 
                                         Array3d<char> *cellstatus);
    void _findSurfaceCellsThread(int startidx, int endidx,
                                 Array3d<char> *cellstatus,
                                 std::vector<GridIndex> *cells);
    void _calculateSurfaceCellStatusThread(int startidx, int endidx,
                                           std::vector<GridIndex> *cells,
                                           Array3d<char> *cellstatus);

    void _calculateNegativeDivergenceVector(std::vector<double> &rhs);
    void _calculateNegativeDivergenceVectorThread(int startidx, 
                                                  int endidx, std::vector<double> *rhs);
    double _getSurfaceTensionTerm(GridIndex g1, GridIndex g2);
    void _calculateMatrixCoefficients(SparseMatrixd &matrix);
    void _calculateMatrixCoefficientsThread(int startidx, int endidx,
                                            SparseMatrixd *matrix);
    bool _solveLinearSystem(SparseMatrixd &matrix, std::vector<double> &rhs, 
                            std::vector<double> &soln);

    bool _solveLinearSystemJacobi(SparseMatrixd &matrix, std::vector<double> &b, 
                                  std::vector<double> &x, int *iterations, double *error);

    void _applyPressureToVelocityFieldMT(FluidMaterialGrid &mgrid, int dir);
    void _applyPressureToVelocityFieldThread(int startidx, int endidx,
                                             FluidMaterialGrid *mgrid, int dir);

    
    int _isize = 0;
    int _jsize = 0;
    int _ksize = 0;
    double _dx = 0;
    double _deltaTime = 0;

    double _pressureSolveTolerance = 1e-9;
    double _pressureSolveAcceptableTolerance = 1.0;
    int _maxCGIterations = 200;
    double _maxtheta = 25;
    int _surfaceTensionClusterThreshold = 36;
    int _blockwidth = 4;

    MACVelocityField *_vFieldFluid;
    MACVelocityField *_vFieldSolid;
    ValidVelocityComponentGrid *_validVelocities;
    Array3d<float> *_liquidSDF;
    WeightGrid *_weightGrid;
    Array3d<float> *_pressureGrid;

    bool _isSurfaceTensionEnabled = false;
    double _surfaceTensionConstant;
    Array3d<float> *_curvatureGrid;

    GridIndexVector _pressureCells;
    int _matSize = 0;
    GridIndexKeyMap _keymap;
    Array3d<char> _surfaceTensionClusterStatus;

    std::string _solverStatus;
    int _solverIterations = 0;
    float _solverError = 0.0f;

};

#endif
