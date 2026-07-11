// Fill out your copyright notice in the Description page of Project Settings.


#include "OSCActorFunctionLibrary.h"

static const FMatrix GL_TO_UE4(FPlane(0, 0, -1, 0), FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, 0, 1));
static const FMatrix GL_TO_UE4_T = GL_TO_UE4.GetTransposed();

bool UOSCActorFunctionLibrary::FloatArrayToMatrix(const TArray<float>& InArray, FMatrix& OutMatrix)
{
	if (InArray.Num() != 16)
		return false;

	FMatrix M;

	const float* Src = InArray.GetData();

	for (int x = 0; x < 4; x++)
	{
		for (int y = 0; y < 4; y++)
		{
			M.M[x][y] = *Src++;
		}
	}

	OutMatrix = M;

	return true;
}

FMatrix UOSCActorFunctionLibrary::TRSToMatrix(float tx, float ty, float tz, float rx, float ry, float rz, float sx, float sy, float sz)
{
	// Directly construct S*R*T without intermediate matrix multiplications.
	// S*R: multiply each row of R by the corresponding scale (row 0 * sx, row 1 * sy, row 2 * sz).
	// Translation goes in the last row.
	const FMatrix R = FRotationMatrix::Make(FRotator(-ry, rz, -rx));
	FMatrix M;
	M.M[0][0] = R.M[0][0] * sx;  M.M[0][1] = R.M[0][1] * sx;  M.M[0][2] = R.M[0][2] * sx;  M.M[0][3] = 0.f;
	M.M[1][0] = R.M[1][0] * sy;  M.M[1][1] = R.M[1][1] * sy;  M.M[1][2] = R.M[1][2] * sy;  M.M[1][3] = 0.f;
	M.M[2][0] = R.M[2][0] * sz;  M.M[2][1] = R.M[2][1] * sz;  M.M[2][2] = R.M[2][2] * sz;  M.M[2][3] = 0.f;
	M.M[3][0] = tx;               M.M[3][1] = ty;               M.M[3][2] = tz;               M.M[3][3] = 1.f;
	return M;
}

FMatrix UOSCActorFunctionLibrary::ConvertGLtoUE4Matrix(const FMatrix& InMatrix)
{
	FMatrix M = InMatrix;
	M.SetOrigin(M.GetOrigin() * 100);
	return GL_TO_UE4 * M * GL_TO_UE4_T;
}
