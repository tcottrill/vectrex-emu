#include "framework.h"
#include "gl_prim_debug.h"
#include "rotation_table.h"
#include "MathUtils.h"
using Vec2 = aae::math::vec2;

#pragma warning(disable : 4244)

// OpenGL Code Goes here.....-------------------------------------------------------------------------
void glCircle(float cx, float cy, float r, int num_segments)
{
	glBegin(GL_LINE_LOOP);
	for (int ii = 0; ii < num_segments; ii++) {
		float theta = 2.0f * 3.1415926f * ii / num_segments;//get the current angle
		float x = r * cosf(theta);//calculate the x component
		float y = r * sinf(theta);//calculate the y component
		glVertex2f(x + cx, y + cy);//output vertex
	}
	glEnd();
}

void glPoint(float x, float y)
{
	glDisable(GL_TEXTURE_2D);
	glColor3f(.5f, 1.0f, .5f);
	glPointSize(4.0f);
	glBegin(GL_POINTS);
	glVertex2f(x, y);
	glEnd();
}

void glLine(float sx, float sy, float ex, float ey)
{
	glBegin(GL_LINES);
	glVertex2f(sx, sy);
	glVertex2f(ex, ey);
	glEnd();
}

void glRectfromCenter(float x, float y, float size)
{
	float MinX = 0.0f;
	float MinY = 0.0f;
	float MaxX = 0.0f;
	float MaxY = 0.0f;

	//Set Origin to Center of Texture
	MinX = x - (size / 2.0f);
	MinY = y - (size / 2.0f);
	MaxX = x + (size / 2.0f);
	MaxY = y + (size / 2.0f);

	glBegin(GL_LINE_LOOP);
	glVertex2f(MinX, MinY);
	glVertex2f(MaxX, MinY);
	glVertex2f(MaxX, MaxY);
	glVertex2f(MinX, MaxY);
	glEnd();
}

void glRect(float xmin, float xmax, float ymin, float ymax)
{
	glBegin(GL_QUADS);
	glVertex2f(xmin, ymin);
	glVertex2f(xmin, ymax);
	glVertex2f(xmax, ymax);
	glVertex2f(xmax, ymin);
	glEnd();
}

void glRectR(float x, float y, float size, int angle)
{
	//Set Origin to Center of Texture
	float MinX = x - (size / 2.0f);
	float MinY = y - (size / 2.0f);
	float MaxX = x + (size / 2.0f);
	float MaxY = y + (size / 2.0f);

	if (angle)
	{
		Vec2 p0 = RotateAroundPoint(MinX, MinY, x, y, _cos[angle], _sin[angle]);
		Vec2 p1 = RotateAroundPoint(MaxX, MinY, x, y, _cos[angle], _sin[angle]);
		Vec2 p2 = RotateAroundPoint(MaxX, MaxY, x, y, _cos[angle], _sin[angle]);
		Vec2 p3 = RotateAroundPoint(MinX, MaxY, x, y, _cos[angle], _sin[angle]);

		glBegin(GL_LINE_LOOP);
		glVertex2f(p0.x, p0.y);
		glVertex2f(p1.x, p1.y);
		glVertex2f(p2.x, p2.y);
		glVertex2f(p3.x, p3.y);
		glEnd();
	}
	else
	{
		glBegin(GL_LINE_LOOP);
		glVertex2f(MinX, MinY);
		glVertex2f(MaxX, MinY);
		glVertex2f(MaxX, MaxY);
		glVertex2f(MinX, MaxY);
		glEnd();
	}
}