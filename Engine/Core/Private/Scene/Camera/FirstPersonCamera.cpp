#include "StdAfx.h"
#include "Scene/Camera/FirstPersonCamera.h"

#include "System/Logger.h"
#include "Platform/OS.h"

namespace SG
{

	FirstPersonCamera::FirstPersonCamera(const Vector3f& position)
		:BasicCamera(position, Vector3f(0.0f))
	{
		UpdateViewMatrix();
	}

	FirstPersonCamera::~FirstPersonCamera()
	{
	}

	bool FirstPersonCamera::OnKeyInputUpdate(EKeyCode keycode, EKeyState keyState)
	{
		if (keyState == EKeyState::ePressed || keyState == EKeyState::eRelease)
			return true;

		switch (keycode)
		{
		case KeyCode_W: mPosition += mFrontVec * mMoveSpeed * mDeltaTime; break;
		case KeyCode_S: mPosition -= mFrontVec * mMoveSpeed * mDeltaTime; break;
		case KeyCode_A: mPosition -= mRightVec * mMoveSpeed * mDeltaTime; break;
		case KeyCode_D: mPosition += mRightVec * mMoveSpeed * mDeltaTime; break;
		case KeyCode_LeftControl: mPosition -= mUpVec * mMoveSpeed * mDeltaTime; break;
		case KeyCode_Space: mPosition += mUpVec * mMoveSpeed * mDeltaTime; break;
		}
		UpdateViewMatrix();
		mbIsViewDirty = true;

		return true;
	}

	bool FirstPersonCamera::OnMouseMoveInputUpdate(int xPos, int yPos, int deltaXPos, int deltaYPos)
	{
		if (Input::IsKeyPressed(KeyCode_MouseLeft))
		{
			if (deltaYPos != 0) 
			{ 
				float dy = -deltaYPos * mRotateSpeed * mDeltaTime * mAspectRatio;
				Matrix4f transform = glm::rotate(Matrix4f(1.0f), dy, mRightVec);
				// do clamp to prevent axis flip
				const float dotFrontVec = glm::dot(Vector3f(glm::normalize(Vector4f(mFrontVec, 0.0f) * transform)), SG_ENGINE_UP_VEC());
				if (dotFrontVec < 0.99f && dotFrontVec > -0.99f)
				{
					mFrontVec = glm::normalize(Vector4f(mFrontVec, 0.0f) * transform);
					mUpVec = glm::normalize(Vector4f(mUpVec, 0.0f) * transform);

					UpdateViewMatrix();
					mbIsViewDirty = true;
				}
			}
			
			if (deltaXPos != 0)
			{
				float dx = deltaXPos * mRotateSpeed * mDeltaTime;
				Matrix4f transform = Matrix3f(glm::rotate(Matrix4f(1.0f), dx, SG_ENGINE_UP_VEC()));
				mFrontVec =  glm::normalize(Vector4f(mFrontVec, 0.0f) * transform);
				mUpVec    =  glm::normalize(Vector4f(mUpVec, 0.0f) * transform);
				mRightVec =  glm::normalize(Vector4f(mRightVec, 0.0f) * transform);
				UpdateViewMatrix();
				mbIsViewDirty = true;
			}
		}
		return true;
	}

	void FirstPersonCamera::UpdateViewMatrix()
	{
		mViewMatrix = BuildViewMatrixCenter(mPosition, mPosition + mFrontVec, SG_ENGINE_UP_VEC());
		CalcFrustum();
		CalcFrustumBoundingBox();
	}

}