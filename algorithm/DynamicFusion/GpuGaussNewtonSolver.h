#pragma once

#include "definations.h"
#include "DynamicFusionParam.h"
#include "WarpField.h"
#include <cublas_v2.h>
#include <cusparse.h>
#include <cusolverDn.h>
namespace dfusion
{
	class GpuGaussNewtonSolver
	{
	public:
		enum
		{
			CTA_SIZE_X = 32,
			CTA_SIZE_Y = 8,
			CTA_SIZE = CTA_SIZE_X * CTA_SIZE_Y,
			VarPerNode = 6,
			LowerPartNum = 21
		};
		struct JrRow2NodeMapper
		{
			ushort nodeId;
			unsigned char k;
			unsigned char ixyz;
		};
	public:
		GpuGaussNewtonSolver();
		~GpuGaussNewtonSolver();

		void init(WarpField* pWarpField, const MapArr& vmap_cano, 
			const MapArr& nmap_cano, Param param, Intr intr);

		// after solve, the interel twists are optimized, but th warpField are not touched
		void solve(const MapArr& vmap_live, const MapArr& nmap_live,
			const MapArr& vmap_warp, const MapArr& nmap_warp);

		// optional, factor out common rigid transformations among all nodes
		void factor_out_rigid();

		// update warpField by calling this function explicitly
		void updateWarpField();

		void reset();

		void debug_print();
		void debug_set_init_x(const float* x_host, int n);
	protected:
		void initSparseStructure();
		void calcDataTerm();
		void calcRegTerm();
		void calcHessian();
		void blockSolve();
		float calcTotalEnergy();

		void checkNan(const DeviceArray<float>& x, int n, const char* msg);

		void bindTextures();
		void unBindTextures();

		static void dumpSparseMatrix(
			std::string name,
			const DeviceArray<int>& rptr, 
			const DeviceArray<int>& cidx,
			const DeviceArray<float>& val, int nRow);

		static void dumpSymLowerMat(std::string name, const DeviceArray<float>& A, int nRowsCols);
		static void dumpMat(std::string name, const DeviceArray<float>& A, int nRowsCols);
		static void dumpVec(std::string name, const DeviceArray<float>& A, int n);
		static void dumpBlocks(std::string name, const DeviceArray<float>& A, int nBlocks, int blockRowCol);
	private:
		WarpField* m_pWarpField;
		const MapArr* m_vmap_cano;
		const MapArr* m_nmap_cano;
		const MapArr* m_vmap_warp;
		const MapArr* m_nmap_warp;
		const MapArr* m_vmap_live;
		const MapArr* m_nmap_live;
		const Param* m_param;
		Intr m_intr;
		int m_numNodes;
		int m_numLv0Nodes;

		// for pre-allocation: allocate a lareger buffer than given nodes
		// to prevent allocation each frame
		int m_nodes_for_buffer;
		int m_not_lv0_nodes_for_buffer;

		DeviceArray2D<WarpField::KnnIdx> m_vmapKnn;
		DeviceArray<WarpField::KnnIdx> m_nodesKnn;
		DeviceArray<float4> m_nodesVw;

		// w.r.t x, the variables we will solve for.
		DeviceArray<float> m_twist;

		// the Hessian matrix representation:
		// H = Jd'Jd + Jr'Jr
		// Jd is the data term jacobi, approximated by diagonal blocks
		// Jr is the regularization term jacobi, sparse.
		// H =	[ Hd  B  ]
		//		[ B^t Hr ]
		// note H is symmetric, thus it is enough to evaluate the lower triangular

		// Hessian of the left-top of data+reg term, 
		// it is a 6x6x? block diags.
		// It is symmetric, during calculation, 
		// we thus only touch the lower part of each block
		// after calculation finished, we filled the upper part then.
		DeviceArray<float> m_Hd;

		// Hessian of the bottom-right of the data+reg term, 
		// it is a dense matrix and symmetric
		// It is symmetric, during calculation, 
		// we thus only touch the lower part of each block
		// after calculation finished, we filled the upper part then.
		DeviceArray<float> m_Hr;
		int m_HrRowsCols;


		// CSR sparse matrix of B
		DeviceArray<float> m_B_val;
		DeviceArray<int> m_B_RowPtr;
		DeviceArray<int> m_B_RowPtr_coo;
		DeviceArray<int> m_B_ColIdx;
		DeviceArray<float> m_Bt_val;
		DeviceArray<int> m_Bt_RowPtr;
		DeviceArray<int> m_Bt_RowPtr_coo;
		DeviceArray<int> m_Bt_ColIdx;
		int m_Brows;
		int m_Bcols;
		int m_Bnnzs;

		// CSR sparse matrix of Jr
		DeviceArray<float> m_Jr_val;
		DeviceArray<int> m_Jr_RowCounter;
		DeviceArray<JrRow2NodeMapper> m_Jr_RowMap2NodeId;
		DeviceArray<int> m_Jr_RowPtr;
		DeviceArray<int> m_Jr_RowPtr_coo;
		DeviceArray<int> m_Jr_ColIdx;

		DeviceArray<float> m_Jrt_val;
		DeviceArray<int> m_Jrt_RowPtr;
		DeviceArray<int> m_Jrt_RowPtr_coo;
		DeviceArray<int> m_Jrt_ColIdx;
		int m_Jrrows;
		int m_Jrcols;
		int m_Jrnnzs;

		cusparseMatDescr_t m_Jrt_desc;
		cusparseMatDescr_t m_Bt_desc;
		cusparseMatDescr_t m_B_desc;

		// let Jr = [Jr0, Jr1]
		//			[0,   Jr3]
		// where Jr0 w.r.t. level-0 nodes
		// thus Jr'Jr = [Jr0'Jr0, Jr0'Jr1          ]
		//				[Jr1'Jr0, Jr1'Jr1 + Jr3'Jr3]
		// Jr0 is a block diagonal matrix, thus the computation
		// of Jr'Jr can be greatly simplified:
		// we simply accumulate Jr0'Jr0 into Hd as the block-diagonal part
		// and B = Jr0'Jr1, which the structure should be evaluated 
		// finally Hd = Jr1'Jr1 + Jr3'Jr3. we accumulate it into a dense matrix

		// m_g = -J^t * f
		// we will solve for H*m_h = m_g
		// and x += step * m_h
		DeviceArray<float> m_g;
		DeviceArray<float> m_h;

		// let H = L*L'
		// we first solve for L*m_u = m_g
		// and then L'*m_h = m_u
		DeviceArray<float> m_u;
		DeviceArray<float> m_tmpvec;

		// energy corresponding to Jr part.
		DeviceArray<float> m_f_r;

		DeviceArray<float> m_energy_vec;

		//// params used in block solver

		// Q = Hr - Bt * Hd^(-1) * B
		// to save storage and a memcpy, 
		// we factor Q = Lq*Lq' and directly store Lq in Q
		DeviceArray<float> m_Q;

		// m_Hd_L: Hd = L*Lt
		DeviceArray<float> m_Hd_Linv;
		DeviceArray<float> m_Hd_LLtinv;

		// inv(Lt) * Bt; 
		// its sparse pattern is the same with Bt
		// we use the same sparse structure with Bt for it
		DeviceArray<float> m_Bt_Ltinv_val;

		//// cusparse handle
		cublasHandle_t m_cublasHandle;
		cusparseHandle_t m_cuSparseHandle;
		cusolverDnHandle_t m_cuSolverHandle;
		DeviceArray<float> m_cuSolverWorkSpace;
	};
}