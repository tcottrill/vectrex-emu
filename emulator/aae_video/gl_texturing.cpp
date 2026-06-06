#include "aae_mame_driver.h"
#include "gl_texturing.h"
#include "opengl_renderer.h"
#include "texture_handler.h"   // [vectrex-port] trimmed local copy
// [vectrex-port] removed: vector_fonts.h (VF font renderer not used here)
#include "colordefs.h"
#include "gl_shader.h" // Added to access the new basic shaders

#pragma warning( disable : 4305 4244 )

int errorsound = 0;

// ----
// Texturing and drawing rectangle code Below.
// ----

// Consolidated drawing routine. Uses standard OpenGL UVs (0=bottom, 1=top).
// Set flip_v = true for FBO copies that need to be inverted.
void drawTexturedQuad(float left, float right, float bottom, float top, bool flip_v = false)
{
	GLfloat vertices[] = {
		left,  bottom,
		left,  top,
		right, top,

		left,  bottom,
		right, top,
		right, bottom
	};

	GLfloat texCoords[12];

	if (flip_v)
	{
		// Flipped UVs (Bottom maps to 1.0, Top maps to 0.0)
		const GLfloat flipped[] = {
			0.0f, 1.0f,
			0.0f, 0.0f,
			1.0f, 0.0f,

			0.0f, 1.0f,
			1.0f, 0.0f,
			1.0f, 1.0f
		};
		memcpy(texCoords, flipped, sizeof(flipped));
	}
	else
	{
		// Standard OpenGL UVs (Bottom maps to 0.0, Top maps to 1.0)
		const GLfloat standard[] = {
			0.0f, 0.0f,
			0.0f, 1.0f,
			1.0f, 1.0f,

			0.0f, 0.0f,
			1.0f, 1.0f,
			1.0f, 0.0f
		};
		memcpy(texCoords, standard, sizeof(standard));
	}

	// Check if a shader is already bound (e.g., fragMulti during final_render)
	GLint current_prog = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &current_prog);
	bool use_basic_shader = (current_prog == 0);

	if (use_basic_shader) {
		bind_shader(fragBasicTex);
		set_uniform1i(fragBasicTex, "u_texture", 0);
	}

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	glVertexPointer(2, GL_FLOAT, 0, vertices);
	glTexCoordPointer(2, GL_FLOAT, 0, texCoords);

	glDrawArrays(GL_TRIANGLES, 0, 6);

	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);

	if (use_basic_shader) {
		unbind_shader();
	}
}

void quad_from_center(float x, float y, float width, float height, int r, int g, int b, int alpha)
{
	float minx = x - (width / 2.0f);
	float miny = y - (height / 2.0f);
	float maxx = x + (width / 2.0f);
	float maxy = y + (height / 2.0f);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
	glColor4ub(r, g, b, alpha);

	GLfloat vertices[] = {
		minx, miny,
		maxx, miny,
		maxx, maxy,

		minx, miny,
		maxx, maxy,
		minx, maxy
	};

	// Check if a shader is already bound
	GLint current_prog = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &current_prog);
	bool use_basic_shader = (current_prog == 0);

	if (use_basic_shader) {
		bind_shader(fragBasicColor);
	}

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, vertices);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glDisableClientState(GL_VERTEX_ARRAY);

	if (use_basic_shader) {
		unbind_shader();
	}
}

void Any_Rect(int facing, int left, int right, int bottom, int top)
{
	drawTexturedQuad((float)left, (float)right, (float)bottom, (float)top, true);
}

void FS_Rect(int facing, int size)
{
	drawTexturedQuad(0.0f, (float)size, 0.0f, (float)size, false);
}

void Screen_Rect(int facing, int size)
{
	drawTexturedQuad(0.0f, (float)size, 0.0f, (float)size, false);
}

void Resize_Rect(int facing, int size)
{
	float h = size * 0.75f;
	drawTexturedQuad(0.0f, (float)size, 0.0f, h, false);
}

void Bezel_Rect(int left, int right, int bottom, int top)
{
	drawTexturedQuad((float)left, (float)right, (float)bottom, (float)top, false);
}

// Eventually I'll get back to this and re-enable it again in some fashion.
void show_error(void)
{
	static int fade = 255;
	static int dir = 0;

	if (have_error) {
		//if (!errorsound) { sample_start(5, num_samples - 4, 0); errorsound = 1; }
		glPushMatrix();
		glLoadIdentity();
		glDisable(GL_TEXTURE_2D);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glColor4ub(40, 0, 0, 220);
		drawTexturedQuad(282.0f, 234.0f, 742.0f, 534.0f);

		glEnable(GL_TEXTURE_2D);
		glColor4ub(255, 255, 255, 255);

		//glBindTexture(GL_TEXTURE_2D, error_tex[0]);
		//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		//glBlendFunc(GL_SRC_ALPHA, GL_ONE); //PROPER
		glTranslatef(375, 475, 0);

		drawTexturedQuad(-24.0f, -24.0f, 24.0f, 24.0f);

		//TODO: Replace this with Vector drawing calls.

		/*
		glLoadIdentity();
		glPrint(418, 457, 255, 255, 255, 255, 1.1, 0, 0, "An Error Occurred!!");
		switch (have_error)
		{
		case 2: glPrint(380, 390, 32, 178, 170, 255, 1.3, 0, 0, "Emu EXE Not Found"); break;
		case 3: glPrint(320, 400, 32, 178, 170, 255, 1.1, 0, 0, "A Config Value is Set Wrong"); break;
		case 10: {
			glPrint(418, 417, 32, 178, 170, 255, 1.1, 0, 0, "REQUIRED ROMS");
			glPrint(418, 380, 32, 178, 170, 255, 1.1, 0, 0, "NOT FOUND!!"); break;
		}
		case 15: glPrint(418, 417, 32, 178, 170, 255, 1.1, 0, 0, "Texture not found"); break;
		case 20: glPrint(330, 405, 32, 178, 170, 255, 1.2, 0, 0, "Sorry, not playable (YET)!"); break;
		}
		glPrint(385, 350, 255, 255, 255, 255, 1.1, 0, 0, "Please see AAE.LOG");
		glPrint(385, 320, 255, 255, 255, 255, 1.1, 0, 0, "for further details.");
		glPrint(365, 235, fade, 255, 255, fade, 1.1, 0, 0, "[Press Exit to Close]");
		*/
		if (dir == 0) {
			fade -= 5;
			if (fade < 40) { dir = 1; }
		}
		if (dir == 1) {
			fade += 5;
			if (fade > 255) { fade = 255; dir = 0; }
		}
		glPopMatrix();
		glLoadIdentity();
		glDisable(GL_TEXTURE_2D);
	}
}