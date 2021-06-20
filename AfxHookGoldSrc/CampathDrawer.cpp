#include "stdafx.h"

#include "CampathDrawer.h"

#include "filming.h"

#include "mirv_time.h"
#include "hooks/HookHw.h"

#include "hlaeFolder.h"

#define _USE_MATH_DEFINES
#include <math.h>

#include <gl/GL.h>


const FLOAT c_CampathCrossRadius = 36.0f;
const FLOAT c_CampathCrossPixelWidth = 4.0f;

const FLOAT c_CameraRadius = c_CampathCrossRadius / 2.0f;
const FLOAT c_CameraPixelWidth = 4.0f;

const FLOAT c_CameraTrajectoryPixelWidth = 8.0f;

/// <summary>Epsilon for point reduction in world units (inch).</summary>
/// <remarks>Must be at least 0.0.</remarks>
const double c_CameraTrajectoryEpsilon = 1.0f;

/// <remarks>Must be at least 2.</remarks>
const size_t c_CameraTrajectoryMaxPointsPerInterval = 1024;


CCampathDrawer g_CampathDrawer;


// CCampathDrawer //////////////////////////////////////////////////////////////

CCampathDrawer::CCampathDrawer()
	: m_Draw(false)
	, m_RebuildDrawing(true)
{
}

CCampathDrawer::~CCampathDrawer()
{
}

void CCampathDrawer::Begin()
{
	g_Filming.GetCamPath()->OnChanged_set(this);
	m_RebuildDrawing = true;
}

void CCampathDrawer::End()
{
	g_Filming.GetCamPath()->OnChanged_set(nullptr);
}

void CCampathDrawer::CamPathChanged(CamPath* obj)
{
	m_RebuildDrawing = true;
}

#define ValCondInv(value,invert) ((invert) ? 1.0f - (value) : (value) )

void CCampathDrawer::Draw(float width, float height, float origin[3], float angles[3])
{
	if (!m_Draw)
		return;

	if (!m_HasDigitsTexture)
	{
		glGenTextures(1, &m_DigitsTexture);
		glBindTexture(GL_TEXTURE_2D, m_DigitsTexture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		if (unsigned char* data = (unsigned char* )malloc(4 * 256 * 128))
		{
			std::wstring fileName = GetHlaeFolderW();
			fileName += L"resources\\hexfont.tga";

			FILE* file;
			_wfopen_s(&file, fileName.c_str(), L"rb");

			if (file)
			{
				fseek(file, 18, SEEK_SET);

				for (int j = 0; j < 128; ++j)
				{
					fread(data + j * 256 * 4, 1, 256 * 4, file);
				}

				fclose(file);
			}

			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

			free(data);
		}

		m_HasDigitsTexture = true;
	}


	// Draw:
	{
		CamPath *camPath = g_Filming.GetCamPath();

		double curTime = Mirv_GetDemoTimeOrClientTime() - camPath->GetOffset();
		bool inCampath = 1 <= camPath->GetSize()
			&& camPath->GetLowerBound() <= curTime
			&& curTime <= camPath->GetUpperBound();
		bool campathCanEval = camPath->CanEval();
		bool campathEnabled = camPath->Enabled_get();
		bool cameraMightBeSelected = false;

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_CULL_FACE);

		if (m_DrawKeyframIndex)
		{
			double vvForward[3];
			double vvRight[3];
			double vvUp[3];

			MakeVectors(angles[2], angles[0], angles[1], vvForward, vvRight, vvUp);

			glBindTexture(GL_TEXTURE_2D, m_DigitsTexture);
			glEnable(GL_ALPHA_TEST);
			glAlphaFunc(GL_GEQUAL, 1.0f / 255.0f);

			glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

			int index = 0;

			for (CamPathIterator it = camPath->GetBegin(); it != camPath->GetEnd(); ++it)
			{
				int digits = 0;
				for (int t = index; 0 < t; t = t / 10)
				{
					++digits;
				}
				if (digits < 1) digits = 1;

				double cpT = it.GetTime();
				CamPathValue cpv = it.GetValue();

				int val = index;

				for (int i = 0; i < digits; ++i)
				{
					int cval = val % 10;
					val = val / 10;

					float left = -0.5f * m_DrawKeyframIndex * (i + 1);
					float top = 0.5f * m_DrawKeyframIndex;
					float bottom = -0.5f * m_DrawKeyframIndex;
					float right = left + 0.5f * m_DrawKeyframIndex;

					float tx = (32 * (cval % 8)) / 256.0f;
					float ty = (64 * (cval / 8)) / 128.0f;

					glBegin(GL_TRIANGLE_STRIP);
					glTexCoord2f(tx, ty);
					glVertex3f((float)cpv.X + left * (float)vvRight[0] + top * (float)vvUp[0], (float)cpv.Y + left * (float)vvRight[1] + top * (float)vvUp[1], (float)cpv.Z + left * (float)vvRight[2] + top * (float)vvUp[2]);
					glTexCoord2f(tx, ty + 64 / 128.0f);
					glVertex3f((float)cpv.X + left * (float)vvRight[0] + bottom * (float)vvUp[0], (float)cpv.Y + left * (float)vvRight[1] + bottom * (float)vvUp[1], (float)cpv.Z + left * (float)vvRight[2] + bottom * (float)vvUp[2]);
					glTexCoord2f(tx + 32 / 256.0f, ty);
					glVertex3f((float)cpv.X + right * (float)vvRight[0] + top * (float)vvUp[0], (float)cpv.Y + right * (float)vvRight[1] + top * (float)vvUp[1], (float)cpv.Z + right * (float)vvRight[2] + top * (float)vvUp[2]);
					glTexCoord2f(tx + 32 / 256.0f, ty + 64 / 128.0f);
					glVertex3f((float)cpv.X + right * (float)vvRight[0] + bottom * (float)vvUp[0], (float)cpv.Y + right * (float)vvRight[1] + bottom * (float)vvUp[1], (float)cpv.Z + right * (float)vvRight[2] + bottom * (float)vvUp[2]);
					glEnd();
				}

				// cameraMightBeSelected = cameraMightBeSelected || lpSelected && cpv.Selected && lpTime <= curTime && curTime <= cpT;

				++index;
			}

			glDisable(GL_ALPHA_TEST);
			glBindTexture(GL_TEXTURE_2D, 0);
		}

		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

		// Draw trajectory:
		if (2 <= camPath->GetSize() && campathCanEval)
		{
			if (m_RebuildDrawing)
			{
				// Rebuild trajectory points.
				// This operation can be quite expensive (up to O(N^2)),
				// so it should be done only when s.th.
				// changed (which is what we do here).

				m_TrajectoryPoints.clear();

				CamPathIterator last = camPath->GetBegin();
				CamPathIterator it = last;

				TempPoint* pts = new TempPoint[c_CameraTrajectoryMaxPointsPerInterval];

				for (++it; it != camPath->GetEnd(); ++it)
				{
					double delta = it.GetTime() - last.GetTime();

					for (size_t i = 0; i < c_CameraTrajectoryMaxPointsPerInterval; i++)
					{
						double t = last.GetTime() + delta * ((double)i / (c_CameraTrajectoryMaxPointsPerInterval - 1));

						CamPathValue cpv = camPath->Eval(t);

						pts[i].t = t;
						pts[i].y = Vector3(cpv.X, cpv.Y, cpv.Z);
						pts[i].nextPt = i + 1 < c_CameraTrajectoryMaxPointsPerInterval ? &(pts[i + 1]) : 0;
					}

					RamerDouglasPeucker(&(pts[0]), &(pts[c_CameraTrajectoryMaxPointsPerInterval - 1]), c_CameraTrajectoryEpsilon);

					// add all points except the last one (to avoid duplicates):
					for (TempPoint* pt = &(pts[0]); pt && pt->nextPt; pt = pt->nextPt)
					{
						m_TrajectoryPoints.push_back(pt->t);
					}

					last = it;
				}

				// add last point:
				m_TrajectoryPoints.push_back(pts[c_CameraTrajectoryMaxPointsPerInterval - 1].t);

				delete[] pts;

				m_RebuildDrawing = false;
			}

			glLineWidth(c_CameraTrajectoryPixelWidth);
			std::list<double>::iterator itPts = m_TrajectoryPoints.begin();

			CamPathIterator itKeysLast = camPath->GetBegin();
			CamPathIterator itKeysNext = itKeysLast;
			++itKeysNext;

			double curPtTime;
			CamPathValue curPtValue;

			glBegin(GL_LINE_STRIP);

			do
			{
				curPtTime = *itPts;
				curPtValue = camPath->Eval(curPtTime);
				++itPts;

				// emit current point:
				{
					double deltaTime = abs(curTime - curPtTime);

					float colour[4];

					// determine colour:
					if (deltaTime < 1.0)
					{
						double t = (deltaTime - 0.0) / 1.0;
						colour[0] = ValCondInv((float)t, curPtValue.Selected);
						colour[1] = ValCondInv(1.0f, curPtValue.Selected);
						colour[2] = ValCondInv(0.0f, curPtValue.Selected);
						colour[3] = (0.5f * (1.0f - (float)t)) + 0.5f;
					}
					else if (deltaTime < 2.0)
					{
						double t = (deltaTime - 1.0) / 1.0;
						colour[0] = ValCondInv(1.0f, curPtValue.Selected);
						colour[1] = ValCondInv(1.0f - (float)t, curPtValue.Selected);
						colour[2] = ValCondInv(0.0f, curPtValue.Selected);
						colour[3] = (0.25f * (1.0f - (float)t)) + 0.25f;
					}
					else
					{
						colour[0] = ValCondInv(1.0f, curPtValue.Selected);
						colour[1] = ValCondInv(0.0f, curPtValue.Selected);
						colour[2] = ValCondInv(0.0f, curPtValue.Selected);
						colour[3] = 0.25f;
					}

					glColor4f(colour[0], colour[1], colour[2], colour[3]);
					glVertex3f(
						(float)curPtValue.X, (float)curPtValue.Y, (float)curPtValue.Z
					);
				}
			} while (itPts != m_TrajectoryPoints.end());

			glEnd();
		}

		// Draw keyframes:
		{
			glLineWidth(c_CampathCrossPixelWidth);

			bool lpSelected = false;
			double lpTime;

			for (CamPathIterator it = camPath->GetBegin(); it != camPath->GetEnd(); ++it)
			{
				double cpT = it.GetTime();
				CamPathValue cpv = it.GetValue();

				cameraMightBeSelected = cameraMightBeSelected || lpSelected && cpv.Selected && lpTime <= curTime && curTime <= cpT;

				lpSelected = cpv.Selected;
				lpTime = cpT;

				double deltaTime = abs(curTime - cpT);

				bool selected = cpv.Selected;

				float colour[4];

				// determine colour:
				if (deltaTime < 1.0)
				{
					double t = (deltaTime - 0.0) / 1.0;
					colour[0] = ValCondInv((float)t, selected);
					colour[1] = ValCondInv(1.0f, selected);
					colour[2] = ValCondInv(0.0f, selected);
					colour[3] = (0.5f * (1.0f - (float)t)) + 0.5f;
				}
				else if (deltaTime < 2.0)
				{
					double t = (deltaTime - 1.0) / 1.0;
					colour[0] = ValCondInv(1.0f, selected);
					colour[1] = ValCondInv(1.0f  - (float)t, selected);
					colour[2] = ValCondInv(0.0f, selected);
					colour[3] = (0.25f * (1.0f - (float)t)) + 0.25f;
				}
				else
				{
					colour[0] = ValCondInv(1.0f, selected);
					colour[1] = ValCondInv(0.0f, selected);
					colour[2] = ValCondInv(0.0f, selected);
					colour[3] = 0.25f;
				}

				glColor4f(colour[0], colour[1], colour[2], colour[3]);

				if (m_DrawKeyframeAxis)
				{
					glBegin(GL_LINES);

					// x / forward line:

					glVertex3f((float)(cpv.X - c_CampathCrossRadius), (float)cpv.Y, (float)cpv.Z);
					glVertex3f((float)(cpv.X + c_CampathCrossRadius), (float)cpv.Y, (float)cpv.Z);

					// y / left line:

					glVertex3f((float)cpv.X, (float)(cpv.Y - c_CampathCrossRadius), (float)cpv.Z);
					glVertex3f((float)cpv.X, (float)(cpv.Y + c_CampathCrossRadius), (float)cpv.Z);

					// z / up line:

					glVertex3f((float)cpv.X, (float)cpv.Y, (float)(cpv.Z - c_CampathCrossRadius));
					glVertex3f((float)cpv.X, (float)cpv.Y, (float)(cpv.Z + c_CampathCrossRadius));

					glEnd();
				}

				if (m_DrawKeyframeCam) DrawCamera(cpv, colour, width, height);
			}

		}

		// Draw wireframe camera:
		if (inCampath && campathCanEval)
		{
			float colourCam[4] = {
				ValCondInv(1.0f, cameraMightBeSelected),
				ValCondInv(campathEnabled ? 0.0f : 1.0f, cameraMightBeSelected),
				ValCondInv(1.0f, cameraMightBeSelected),
				0.5f
			};

			CamPathValue cpv = camPath->Eval(curTime);

			DrawCamera(cpv, colourCam, width, height);
		}

		glEnable(GL_CULL_FACE);
		glLineWidth(1.0f);
		glDisable(GL_BLEND);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
}


void CCampathDrawer::DrawCamera(const CamPathValue& cpv, const float colour[4], float width, float height)
{
	glLineWidth(c_CameraPixelWidth);

	// limit to values as RenderView hook:
	double fov = min(179, max(1, cpv.Fov));

	double forward[3], right[3], up[3];
	QEulerAngles ang = cpv.R.ToQREulerAngles().ToQEulerAngles();
	MakeVectors(ang.Roll, ang.Pitch, ang.Yaw, forward, right, up);

	Vector3 vCp(cpv.X, cpv.Y, cpv.Z);
	Vector3 vForward(forward);
	Vector3 vUp(up);
	Vector3 vRight(right);

	double a = sin(fov * M_PI / 360.0) * c_CameraRadius;
	double b = a;

	double aspectRatio = width ? (double)height / (double)width : 1.0;

	b *= aspectRatio;

	Vector3 vLU = vCp + (double)c_CameraRadius * vForward - a * vRight + b * vUp;
	Vector3 vRU = vCp + (double)c_CameraRadius * vForward + a * vRight + b * vUp;
	Vector3 vLD = vCp + (double)c_CameraRadius * vForward - a * vRight - b * vUp;
	Vector3 vRD = vCp + (double)c_CameraRadius * vForward + a * vRight - b * vUp;
	Vector3 vMU = vLU + (vRU - vLU) / 2;
	Vector3 vMUU = vMU + 0.5 * b * vUp;

	glColor4f(colour[0], colour[1], colour[2], colour[3]);

	glBegin(GL_LINES);
	glVertex3f((float)vCp.X, (float)vCp.Y, (float)vCp.Z);
	glVertex3f((float)vLD.X, (float)vLD.Y, (float)vLD.Z);

	glVertex3f((float)vCp.X, (float)vCp.Y, (float)vCp.Z);
	glVertex3f((float)vRD.X, (float)vRD.Y, (float)vRD.Z);

	glVertex3f((float)vCp.X, (float)vCp.Y, (float)vCp.Z);
	glVertex3f((float)vLU.X, (float)vLU.Y, (float)vLU.Z);

	glVertex3f((float)vCp.X, (float)vCp.Y, (float)vCp.Z);
	glVertex3f((float)vRU.X, (float)vRU.Y, (float)vRU.Z);

	glVertex3f((float)vLD.X, (float)vLD.Y, (float)vLD.Z);
	glVertex3f((float)vRD.X, (float)vRD.Y, (float)vRD.Z);

	glVertex3f((float)vRD.X, (float)vRD.Y, (float)vRD.Z);
	glVertex3f((float)vRU.X, (float)vRU.Y, (float)vRU.Z);

	glVertex3f((float)vRU.X, (float)vRU.Y, (float)vRU.Z);
	glVertex3f((float)vLU.X, (float)vLU.Y, (float)vLU.Z);

	glVertex3f((float)vLU.X, (float)vLU.Y, (float)vLU.Z);
	glVertex3f((float)vLD.X, (float)vLD.Y, (float)vLD.Z);

	glVertex3f((float)vLU.X, (float)vLU.Y, (float)vLU.Z);
	glVertex3f((float)vMUU.X, (float)vMUU.Y, (float)vMUU.Z);

	glVertex3f((float)vRU.X, (float)vRU.Y, (float)vRU.Z);
	glVertex3f((float)vMUU.X, (float)vMUU.Y, (float)vMUU.Z);

	glEnd();
}

void CCampathDrawer::RamerDouglasPeucker(TempPoint* start, TempPoint* end, double epsilon)
{
	double dmax = 0;
	TempPoint* index = start;

	for (TempPoint* i = start->nextPt; i && i != end; i = i->nextPt)
	{
		double d = ShortestDistanceToSegment(i, start, end);
		if (d > dmax)
		{
			index = i;
			dmax = d;
		}
	}

	// If max distance is greater than epsilon, recursively simplify
	if (dmax > epsilon)
	{
		RamerDouglasPeucker(start, index, epsilon);
		RamerDouglasPeucker(index, end, epsilon);
	}
	else {
		start->nextPt = end;
	}
}

double CCampathDrawer::ShortestDistanceToSegment(TempPoint* pt, TempPoint* start, TempPoint* end)
{
	double ESx = end->y.X - start->y.X;
	double ESy = end->y.Y - start->y.Y;
	double ESz = end->y.Z - start->y.Z;
	double dESdES = ESx * ESx + ESy * ESy + ESz * ESz;
	double t = dESdES ? (
		(pt->y.X - start->y.X) * ESx + (pt->y.Y - start->y.Y) * ESy + (pt->y.Z - start->y.Z) * ESz
		) / dESdES : 0.0;

	if (t <= 0.0)
		return (start->y - pt->y).Length();
	else
		if (1.0 <= t)
			return (pt->y - end->y).Length();

	return (pt->y - (start->y + t * (end->y - start->y))).Length();
}
