// SDL2 + GLES2 window test: heavy-ish fragment shader, prints fps. Run via gpuwin on the phone's sway session.
#include <SDL.h>
#include <SDL_opengles2.h>
#include <stdio.h>
#include <stdlib.h>
static GLuint sh(GLenum t, const char *s) { GLuint h = glCreateShader(t); glShaderSource(h, 1, &s, 0); glCompileShader(h); return h; }
int main(int argc, char **argv)
{
	int W = argc > 1 ? atoi(argv[1]) : 360, H = argc > 2 ? atoi(argv[2]) : 640, secs = argc > 3 ? atoi(argv[3]) : 8;
	setvbuf(stdout, 0, _IONBF, 0);
	if (SDL_Init(SDL_INIT_VIDEO)) { printf("SDL_Init: %s\n", SDL_GetError()); return 1; }
	printf("video driver: %s\n", SDL_GetCurrentVideoDriver());
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_Window *w = SDL_CreateWindow("gl", 0, 0, W, H, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
	if (!w) { printf("window: %s\n", SDL_GetError()); return 2; }
	SDL_GLContext c = SDL_GL_CreateContext(w);
	if (!c) { printf("context: %s\n", SDL_GetError()); return 3; }
	SDL_GL_SetSwapInterval(0);
	printf("GL_RENDERER=%s\n", glGetString(GL_RENDERER));
	GLuint p = glCreateProgram();
	glAttachShader(p, sh(GL_VERTEX_SHADER, "attribute vec2 a;varying vec2 v;void main(){v=a;gl_Position=vec4(a,0.,1.);}"));
	glAttachShader(p, sh(GL_FRAGMENT_SHADER, "precision mediump float;varying vec2 v;uniform float t;void main(){vec2 q=v*3.;float x=sin(q.x+t)+cos(q.y*1.3-t);gl_FragColor=vec4(.5+.5*sin(x*2.),.5+.5*cos(x),.5+.5*sin(x+q.x),1.);}"));
	glLinkProgram(p); glUseProgram(p);
	float q[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, q);
	GLint tu = glGetUniformLocation(p, "t");
	int f = 0; Uint32 t0 = SDL_GetTicks();
	while (SDL_GetTicks() - t0 < (Uint32)secs * 1000) {
		SDL_Event e; while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) goto out;
		glUniform1f(tu, f * 0.05f);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		SDL_GL_SwapWindow(w); f++;
	}
out:
	printf("%d frames, %.1f fps\n", f, f * 1000.0 / (SDL_GetTicks() - t0));
	return 0;
}
