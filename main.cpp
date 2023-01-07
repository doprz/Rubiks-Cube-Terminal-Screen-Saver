// Rubiks-Cube-Terminal-Screen-Saver
// Copyright (C) 2023 doprz
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <iostream>
#include <cstdio>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <chrono>
#include <thread>

#include <math.h>
#include <vector>
#include <numeric>
#include <string>

struct Vector3f {
    float x, y, z;
};

struct Dim2i {
    int w, h;
};

namespace ANSI_escape_code {
    // Cursor Functions
    const char *SET_CURSOR_HOME = "\x1b[H";
    const char *CURSOR_VISIBLE = "\x1b[?25h";
    const char *CURSOR_INVISIBLE = "\x1b[?25l";

    // Starts on row = 1, col = 1
    void setCursorPos(int row = 1, int col = 1) {
        printf("\x1b[%d;%dH", row, col);
    }

    // Erase Functions
    const char *ERASE_SCREEN = "\x1b[2J";
    const char *ERASE_CURRENT_LINE = "\x1b[2K";
    const char *ERASE_LINE_START_TO_CURSOR = "\x1b[1K";

    // Common Private Modes
    const char *ENABLE_ALT_BUFFER = "\x1b[?1049h";
    const char *DISABLE_ALT_BUFFER = "\x1b[?1049l";

    namespace color {
        // Regular
        const char *RED = "\x1B[31m";
        const char *GREEN = "\x1B[32m";
        const char *YELLOW = "\x1B[33m";
        const char *BLUE = "\x1B[34m";
        const char *MAGENTA = "\x1B[35m";
        const char *CYAN = "\x1B[36m";
        const char *WHITE = "\x1B[37m";
        const char *BLACK = "\x1B[30m";
        const char *RESET = "\x1B[0m";
        
        const char *BOLD_RED = "\x1B[1;31m";
        const char *BOLD_GREEN = "\x1B[1;32m";
        const char *BOLD_YELLOW = "\x1B[1;33m";
        const char *BOLD_BLUE = "\x1B[1;34m";
        const char *BOLD_MAGENTA = "\x1B[1;35m";
        const char *BOLD_CYAN = "\x1B[1;36m";
        const char *BOLD_WHITE = "\x1B[1;37m";
        const char *BOLD_BLACK = "\x1B[1;30m";
    }
}

class Timer {
    private:
        std::chrono::time_point<std::chrono::steady_clock> start, end;
        std::chrono::duration<int, std::micro> duration;
        std::vector<int> *durationVector;
    public:
        Timer(std::vector<int> *_durationVector = nullptr) : start(std::chrono::high_resolution_clock::now()), durationVector(_durationVector) {}
        ~Timer() {
            end = std::chrono::high_resolution_clock::now();
            duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

            if (durationVector != nullptr) {
                durationVector->push_back(duration.count());
            }

            float fps = 1000000 / static_cast<float>(duration.count());

            ANSI_escape_code::setCursorPos(1, 1 + 10);
            printf("%s%s\r", ANSI_escape_code::color::RESET, ANSI_escape_code::ERASE_LINE_START_TO_CURSOR);
            printf("%*.*ffps", 7, 2, fps);

            ANSI_escape_code::setCursorPos(2, 1 + 24);
            printf("%s%s\r", ANSI_escape_code::color::RESET, ANSI_escape_code::ERASE_LINE_START_TO_CURSOR);
            printf("%*.*fms (%10dus)", 7, 2, duration.count() / 1000.0f, duration.count());
        }
};

#define PROFILING 1
#if PROFILING
#define PROFILE_SCOPE(durationVector) Timer timer##__LINE__(&durationVector)
#else
#define PROFILE_SCOPE(durationVector)
#endif

bool showDebugInfo = true;

int WIDTH = 50;
int HEIGHT = 25;

std::vector<char> buffer((WIDTH * HEIGHT), ' ');
std::vector<char> buffer_prev((WIDTH * HEIGHT), ' ');
std::vector<std::string_view> cbuffer((WIDTH * HEIGHT), ANSI_escape_code::color::RESET);
std::vector<std::string_view> cbuffer_prev((WIDTH * HEIGHT), ANSI_escape_code::color::RESET);
std::vector<float> zbuffer((WIDTH * HEIGHT), 0);

const float CUBE_SIZE = 1.0f; // Unit Cube
float SPACING = 3.0f / WIDTH;
const float GRID_SPACING = 0.04f;
const char *GRID_LINE_COLOR = ANSI_escape_code::color::BLACK;

const float K2 = 10.0f;
float K1 = (WIDTH * K2 * 3) / (8 * (sqrt(3)*CUBE_SIZE));

Vector3f lightSource = {0.0f, 1.0f, -1.0f};
Vector3f rotatedLightSource = {0.0f, 1.0f, -1.0f};
std::vector<float> trigValues(6);

const float FPS_LIMIT = 60.0f;
const float FRAME_DURATION_MICRO = 1000000.0f / (FPS_LIMIT ? FPS_LIMIT : 1);
std::chrono::time_point<std::chrono::steady_clock> nextFrame, previousFrame;
std::vector<int> frameTimes;

static uint32_t allocCount = 0;
void *operator new(size_t size) {
    allocCount++;
    return malloc(size);
}

float getVectorMag(Vector3f vec) {
    float x = vec.x;
    float y = vec.y;
    float z = vec.z;

    return sqrt(x*x + y*y + z*z);
}

void normVector(Vector3f &vec) {
    float x = vec.x;
    float y = vec.y;
    float z = vec.z;

    float mag = getVectorMag(vec);
    // one over mag
    float oomag = 1 / mag;

    if (mag > 0) {
        // vec = {x/mag, y/mag, z/mag};
        vec = {x * oomag, y * oomag, z * oomag};
    }
}

void updateBuffers(float i, float j, float k, std::vector<float> &trigValues, std::vector<char> &buffer, std::vector<float> &zbuffer, std::vector<std::string_view> &cbuffer, std::string_view color, float luminance) {
    float sinA = trigValues[0], cosA = trigValues[1];
    float sinB = trigValues[2], cosB = trigValues[3];
    float sinC = trigValues[4], cosC = trigValues[5];

    float x = cosA*cosB*j + (cosA*sinB*sinC - sinA*cosC)*i + (cosA*sinB*cosC + sinA*sinC)*k;
    float y = sinA*cosB*j + (sinA*sinB*sinC + cosA*cosC)*i + (sinA*sinB*cosC - cosA*sinC)*k;
    float z = -j*sinB + i*cosB*sinC + k*cosB*cosC + K2;

    float ooz = 1 / z; // "One over z"

    int xp = static_cast<int>((WIDTH/2) + (K1*ooz*x));
    int yp = static_cast<int>((HEIGHT/2) - (K1*ooz*y));

    int index = xp + yp * WIDTH;
    std::vector<char>::size_type indexLimit = buffer.size();

    std::vector<char>::iterator bufferIter = buffer.begin();
    std::vector<std::string_view>::iterator cbufferIter = cbuffer.begin();
    std::vector<float>::iterator zbufferIter = zbuffer.begin();

    // Luminance ranges from -1 to +1 for the dot product of the plane normal and light source normalized 3D unit vectors
    // If the luminance > 0, then the plane is facing towards the light source
    // else if luminance < 0, then the plane is facing away from the light source
    // else if luminance = 0, then the plane and the light source are perpendicular
    int luminance_index = luminance * 11;
    if (index >= 0 && index < indexLimit) {
        if (ooz > zbuffer[index]) {
            *(zbufferIter + index) = ooz;
            *(cbufferIter + index) = color;
            *(bufferIter + index) = ".,-~:;=!*#$@"[luminance > 0 ? luminance_index : 0];
        }
    }
}

void renderCubeAxis_A(std::vector<float> &trigValues, std::vector<char> &buffer, std::vector<float> &zbuffer, std::vector<std::string_view> &cbuffer, std::string_view color1, std::string_view color2) {
    float sinA = trigValues[0], cosA = trigValues[1];
    float sinB = trigValues[2], cosB = trigValues[3];
    float sinC = trigValues[4], cosC = trigValues[5];

    Vector3f surfaceNormal_front = {0.0f, 0.0f, CUBE_SIZE};
    Vector3f surfaceNormal_back = {0.0f, 0.0f, -CUBE_SIZE};

    Vector3f rotatedSurfaceNormal_front = {
        cosA*cosB*surfaceNormal_front.x + (cosA*sinB*sinC - sinA*cosC)*surfaceNormal_front.y + (cosA*sinB*cosC + sinA*sinC)*surfaceNormal_front.z,
        sinA*cosB*surfaceNormal_front.x + (sinA*sinB*sinC + cosA*cosC)*surfaceNormal_front.y + (sinA*sinB*cosC - cosA*sinC)*surfaceNormal_front.z,
        -surfaceNormal_front.x*sinB + surfaceNormal_front.y*cosB*sinC + surfaceNormal_front.z*cosB*cosC
    };
    normVector(rotatedSurfaceNormal_front);

    Vector3f rotatedSurfaceNormal_back = {
        cosA*cosB*surfaceNormal_back.x + (cosA*sinB*sinC - sinA*cosC)*surfaceNormal_back.y + (cosA*sinB*cosC + sinA*sinC)*surfaceNormal_back.z,
        sinA*cosB*surfaceNormal_back.x + (sinA*sinB*sinC + cosA*cosC)*surfaceNormal_back.y + (sinA*sinB*cosC - cosA*sinC)*surfaceNormal_back.z,
        -surfaceNormal_back.x*sinB + surfaceNormal_back.y*cosB*sinC + surfaceNormal_back.z*cosB*cosC
    };
    normVector(rotatedSurfaceNormal_back);

    float luminance_front = rotatedSurfaceNormal_front.x*rotatedLightSource.x + rotatedSurfaceNormal_front.y*rotatedLightSource.y + rotatedSurfaceNormal_front.z*rotatedLightSource.z;
    float luminance_back = rotatedSurfaceNormal_back.x*rotatedLightSource.x + rotatedSurfaceNormal_back.y*rotatedLightSource.y + rotatedSurfaceNormal_back.z*rotatedLightSource.z;

    // z
    float k = CUBE_SIZE/2;

    // y
    for (float i = -CUBE_SIZE/2; i <= CUBE_SIZE/2; i+=SPACING) {
        // x
        for (float j = -CUBE_SIZE/2; j <= CUBE_SIZE/2; j+=SPACING) {
            std::string_view charColor1 = color1;
            std::string_view charColor2 = color2;
            if (i > (-CUBE_SIZE/2 + CUBE_SIZE/3) - GRID_SPACING &&
                    i < (-CUBE_SIZE/2 + CUBE_SIZE/3) + GRID_SPACING) {
                charColor1 = GRID_LINE_COLOR;
                charColor2 = GRID_LINE_COLOR;
            } else if (i > (CUBE_SIZE/2 - CUBE_SIZE/3) - GRID_SPACING &&
                    i < (CUBE_SIZE/2 - CUBE_SIZE/3) + GRID_SPACING) {
                charColor1 = GRID_LINE_COLOR;
                charColor2 = GRID_LINE_COLOR;
            } else if (j > (-CUBE_SIZE/2 + CUBE_SIZE/3) - GRID_SPACING &&
                    j < (-CUBE_SIZE/2 + CUBE_SIZE/3) + GRID_SPACING) {
                charColor1 = GRID_LINE_COLOR;
                charColor2 = GRID_LINE_COLOR;
            } else if (j > (CUBE_SIZE/2 - CUBE_SIZE/3) - GRID_SPACING &&
                    j < (CUBE_SIZE/2 - CUBE_SIZE/3) + GRID_SPACING) {
                charColor1 = GRID_LINE_COLOR;
                charColor2 = GRID_LINE_COLOR;
            }

            /* Front Face */
            updateBuffers(i, j, k, trigValues, buffer, zbuffer, cbuffer, charColor1, luminance_front);

            /* Back Face */
            updateBuffers(i, j, -k, trigValues, buffer, zbuffer, cbuffer, charColor2, luminance_back);
        }
    }
}

void renderCubeAxis_B(std::vector<float> &trigValues, std::vector<char> &buffer, std::vector<float> &zbuffer, std::vector<std::string_view> &cbuffer, std::string_view color1, std::string_view color2) {
    float sinA = trigValues[0], cosA = trigValues[1];
    float sinB = trigValues[2], cosB = trigValues[3];
    float sinC = trigValues[4], cosC = trigValues[5];

    Vector3f surfaceNormal_front = {0.0f, CUBE_SIZE, 0.0f};
    Vector3f surfaceNormal_back = {0.0f, -CUBE_SIZE, 0.0f};

    Vector3f rotatedSurfaceNormal_front = {
        cosA*cosB*surfaceNormal_front.x + (cosA*sinB*sinC - sinA*cosC)*surfaceNormal_front.y + (cosA*sinB*cosC + sinA*sinC)*surfaceNormal_front.z,
        sinA*cosB*surfaceNormal_front.x + (sinA*sinB*sinC + cosA*cosC)*surfaceNormal_front.y + (sinA*sinB*cosC - cosA*sinC)*surfaceNormal_front.z,
        -surfaceNormal_front.x*sinB + surfaceNormal_front.y*cosB*sinC + surfaceNormal_front.z*cosB*cosC
    };
    normVector(rotatedSurfaceNormal_front);

    Vector3f rotatedSurfaceNormal_back = {
        cosA*cosB*surfaceNormal_back.x + (cosA*sinB*sinC - sinA*cosC)*surfaceNormal_back.y + (cosA*sinB*cosC + sinA*sinC)*surfaceNormal_back.z,
        sinA*cosB*surfaceNormal_back.x + (sinA*sinB*sinC + cosA*cosC)*surfaceNormal_back.y + (sinA*sinB*cosC - cosA*sinC)*surfaceNormal_back.z,
        -surfaceNormal_back.x*sinB + surfaceNormal_back.y*cosB*sinC + surfaceNormal_back.z*cosB*cosC
    };
    normVector(rotatedSurfaceNormal_back);

    float luminance_front = rotatedSurfaceNormal_front.x*rotatedLightSource.x + rotatedSurfaceNormal_front.y*rotatedLightSource.y + rotatedSurfaceNormal_front.z*rotatedLightSource.z;
    float luminance_back = rotatedSurfaceNormal_back.x*rotatedLightSource.x + rotatedSurfaceNormal_back.y*rotatedLightSource.y + rotatedSurfaceNormal_back.z*rotatedLightSource.z;

    // y
    float i = CUBE_SIZE/2;

    // x
    for (float j = -CUBE_SIZE/2; j <= CUBE_SIZE/2; j+=SPACING) {
        // z
        for (float k = -CUBE_SIZE/2; k <= CUBE_SIZE/2; k+=SPACING) {
            std::string_view charColor1 = color1;
            std::string_view charColor2 = color2;
            if (j > (-CUBE_SIZE/2 + CUBE_SIZE/3) - GRID_SPACING &&
                    j < (-CUBE_SIZE/2 + CUBE_SIZE/3) + GRID_SPACING) {
                charColor1 = GRID_LINE_COLOR;
                charColor2 = GRID_LINE_COLOR;
            } else if (j > (CUBE_SIZE/2 - CUBE_SIZE/3) - GRID_SPACING &&
                    j < (CUBE_SIZE/2 - CUBE_SIZE/3) + GRID_SPACING) {
                charColor1 = GRID_LINE_COLOR;
                charColor2 = GRID_LINE_COLOR;
            } else if (k > (-CUBE_SIZE/2 + CUBE_SIZE/3) - GRID_SPACING &&
                    k < (-CUBE_SIZE/2 + CUBE_SIZE/3) + GRID_SPACING) {
                charColor1 = GRID_LINE_COLOR;
                charColor2 = GRID_LINE_COLOR;
            } else if (k > (CUBE_SIZE/2 - CUBE_SIZE/3) - GRID_SPACING &&
                    k < (CUBE_SIZE/2 - CUBE_SIZE/3) + GRID_SPACING) {
                charColor1 = GRID_LINE_COLOR;
                charColor2 = GRID_LINE_COLOR;
            }

            /* Front Face */
            updateBuffers(i, j, k, trigValues, buffer, zbuffer, cbuffer, charColor1, luminance_front);

            /* Back Face */
            updateBuffers(-i, j, k, trigValues, buffer, zbuffer, cbuffer, charColor2, luminance_back);
        }
    }
}

void renderCubeAxis_C(std::vector<float> &trigValues, std::vector<char> &buffer, std::vector<float> &zbuffer, std::vector<std::string_view> &cbuffer, std::string_view color1, std::string_view color2) {
    float sinA = trigValues[0], cosA = trigValues[1];
    float sinB = trigValues[2], cosB = trigValues[3];
    float sinC = trigValues[4], cosC = trigValues[5];

    Vector3f surfaceNormal_front = {CUBE_SIZE, 0.0f, 0.0f};
    Vector3f surfaceNormal_back = {-CUBE_SIZE, 0.0f, 0.0f};

    Vector3f rotatedSurfaceNormal_front = {
        cosA*cosB*surfaceNormal_front.x + (cosA*sinB*sinC - sinA*cosC)*surfaceNormal_front.y + (cosA*sinB*cosC + sinA*sinC)*surfaceNormal_front.z,
        sinA*cosB*surfaceNormal_front.x + (sinA*sinB*sinC + cosA*cosC)*surfaceNormal_front.y + (sinA*sinB*cosC - cosA*sinC)*surfaceNormal_front.z,
        -surfaceNormal_front.x*sinB + surfaceNormal_front.y*cosB*sinC + surfaceNormal_front.z*cosB*cosC
    };
    normVector(rotatedSurfaceNormal_front);

    Vector3f rotatedSurfaceNormal_back = {
        cosA*cosB*surfaceNormal_back.x + (cosA*sinB*sinC - sinA*cosC)*surfaceNormal_back.y + (cosA*sinB*cosC + sinA*sinC)*surfaceNormal_back.z,
        sinA*cosB*surfaceNormal_back.x + (sinA*sinB*sinC + cosA*cosC)*surfaceNormal_back.y + (sinA*sinB*cosC - cosA*sinC)*surfaceNormal_back.z,
        -surfaceNormal_back.x*sinB + surfaceNormal_back.y*cosB*sinC + surfaceNormal_back.z*cosB*cosC
    };
    normVector(rotatedSurfaceNormal_back);

    float luminance_front = rotatedSurfaceNormal_front.x*rotatedLightSource.x + rotatedSurfaceNormal_front.y*rotatedLightSource.y + rotatedSurfaceNormal_front.z*rotatedLightSource.z;
    float luminance_back = rotatedSurfaceNormal_back.x*rotatedLightSource.x + rotatedSurfaceNormal_back.y*rotatedLightSource.y + rotatedSurfaceNormal_back.z*rotatedLightSource.z;

    // x
    float j = CUBE_SIZE/2;

    // z
    for (float k = -CUBE_SIZE/2; k <= CUBE_SIZE/2; k+=SPACING) {
        // y
        for (float i = -CUBE_SIZE/2; i <= CUBE_SIZE/2; i+=SPACING) {
            std::string_view charColor1 = color1;
            std::string_view charColor2 = color2;
            if (k > (-CUBE_SIZE/2 + CUBE_SIZE/3) - GRID_SPACING &&
                    k < (-CUBE_SIZE/2 + CUBE_SIZE/3) + GRID_SPACING) {
                charColor1 = GRID_LINE_COLOR;
                charColor2 = GRID_LINE_COLOR;
            } else if (k > (CUBE_SIZE/2 - CUBE_SIZE/3) - GRID_SPACING &&
                    k < (CUBE_SIZE/2 - CUBE_SIZE/3) + GRID_SPACING) {
                charColor1 = GRID_LINE_COLOR;
                charColor2 = GRID_LINE_COLOR;
            } else if (i > (-CUBE_SIZE/2 + CUBE_SIZE/3) - GRID_SPACING &&
                    i < (-CUBE_SIZE/2 + CUBE_SIZE/3) + GRID_SPACING) {
                charColor1 = GRID_LINE_COLOR;
                charColor2 = GRID_LINE_COLOR;
            } else if (i > (CUBE_SIZE/2 - CUBE_SIZE/3) - GRID_SPACING &&
                    i < (CUBE_SIZE/2 - CUBE_SIZE/3) + GRID_SPACING) {
                charColor1 = GRID_LINE_COLOR;
                charColor2 = GRID_LINE_COLOR;
            }

            /* Front Face */
            updateBuffers(i, j, k, trigValues, buffer, zbuffer, cbuffer, charColor1, luminance_front);

            /* Back Face */
            updateBuffers(i, -j, k, trigValues, buffer, zbuffer, cbuffer, charColor2, luminance_back);
        }
    }
}

void renderFrame(std::vector<char> &buffer, std::vector<char> &buffer_prev, std::vector<std::string_view> &cbuffer, std::vector<std::string_view> &cbuffer_prev, std::vector<float> &zbuffer, std::vector<float> &trigValues) {
    buffer_prev = buffer;
    cbuffer_prev = cbuffer;

    std::fill(buffer.begin(), buffer.end(), ' ');
    std::fill(cbuffer.begin(), cbuffer.end(), ANSI_escape_code::color::RESET);
    std::fill(zbuffer.begin(), zbuffer.end(), 0);

    renderCubeAxis_A(trigValues, buffer, zbuffer, cbuffer, ANSI_escape_code::color::YELLOW, ANSI_escape_code::color::WHITE);
    renderCubeAxis_B(trigValues, buffer, zbuffer, cbuffer, ANSI_escape_code::color::GREEN, ANSI_escape_code::color::BLUE);
    renderCubeAxis_C(trigValues, buffer, zbuffer, cbuffer, ANSI_escape_code::color::BOLD_RED, ANSI_escape_code::color::RED);

    printf("%s", ANSI_escape_code::SET_CURSOR_HOME);

    std::vector<char>::iterator buffer_prev_iter = buffer_prev.begin();
    std::vector<std::string_view>::iterator cbuffer_iter = cbuffer.begin();
    std::vector<std::string_view>::iterator cbuffer_prev_iter = cbuffer_prev.begin();

    for (std::vector<char>::iterator bufferIter = buffer.begin(); bufferIter != buffer.end(); bufferIter++) {
        size_t index = bufferIter - buffer.begin();
        if ((*bufferIter == *(buffer_prev_iter + index)) && 
            (*(cbuffer_iter + index) == *(cbuffer_prev_iter + index))) {
            continue;
        }

        int x = index % WIDTH;
        int y = index / WIDTH;

        // Move cursor, add color, and print char
        printf("\x1b[%d;%dH%s%s%c", y+1, x+1, ANSI_escape_code::color::RESET, (cbuffer_iter + index)->data(), *bufferIter);
    }
}

void clearBuffers(std::vector<char> &buffer, std::vector<char> &buffer_prev, std::vector<std::string_view> &cbuffer, std::vector<std::string_view> &cbuffer_prev, std::vector<float> &zbuffer) {
    std::fill(buffer.begin(), buffer.end(), ' ');
    std::fill(buffer_prev.begin(), buffer_prev.end(), ' ');
    std::fill(cbuffer.begin(), cbuffer.end(), ANSI_escape_code::color::RESET);
    std::fill(cbuffer_prev.begin(), cbuffer_prev.end(), ANSI_escape_code::color::RESET);
    std::fill(zbuffer.begin(), zbuffer.end(), 0);
}

void resizeBuffers(std::vector<char> &buffer, std::vector<char> &buffer_prev, std::vector<std::string_view> &cbuffer, std::vector<std::string_view> &cbuffer_prev, std::vector<float> &zbuffer) {
    buffer.resize((WIDTH * HEIGHT));
    buffer_prev.resize((WIDTH * HEIGHT));
    cbuffer.resize((WIDTH * HEIGHT));
    cbuffer_prev.resize((WIDTH * HEIGHT));
    zbuffer.resize((WIDTH * HEIGHT));
}

Dim2i getTerminalDim() {
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) != 0 &&
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws ) != 0 &&
        ioctl(STDERR_FILENO, TIOCGWINSZ, &ws ) != 0 ) {
        fprintf(stderr, "ioctl() failed\n");
        return {0, 0};
    }

    return {ws.ws_col, ws.ws_row};
}

void updateDim() {
    Dim2i terminalDim = getTerminalDim();

    if (terminalDim.w && terminalDim.h) {
        WIDTH = terminalDim.w;
        HEIGHT = terminalDim.h;
        K1 = (WIDTH * K2 * 3) / (8 * (sqrt(3)*CUBE_SIZE));
        SPACING = 3.0f / WIDTH;

        resizeBuffers(buffer, buffer_prev, cbuffer, cbuffer_prev, zbuffer);
        clearBuffers(buffer, buffer_prev, cbuffer, cbuffer_prev, zbuffer);
        printf("%s", ANSI_escape_code::ERASE_SCREEN);
    }
}

void handleExit() {
    printf("%s", ANSI_escape_code::ERASE_SCREEN);
    printf("%s", ANSI_escape_code::DISABLE_ALT_BUFFER);

    printf("%s", ANSI_escape_code::color::RESET);
    printf("%s", ANSI_escape_code::CURSOR_VISIBLE);

    if (showDebugInfo) {
        printf("Width: %d | Height: %d\n", WIDTH, HEIGHT);
        printf("K1: %f | K2: %f | Spacing: %f | Grid Spacing: %f | Buffer Size: %d\n", K1, K2, SPACING, GRID_SPACING, (WIDTH * HEIGHT));
        printf("Memory Allocations: %d\n", allocCount);

        uint32_t frames = frameTimes.size();
        float frameAvg = std::reduce(frameTimes.begin(), frameTimes.end()) / (frames ? frames : 1);
        std::cout << "Frames: " << frames 
            << " | Frame Average: " << (frameAvg / 1000.0f) << " milliseconds (" << frameAvg << " microseconds)" 
            << " | Average FPS: " << (frames ? (1000000 / static_cast<float>(frameAvg)) : 0) << std::endl;
    }
}

void SIGINTCallbackEventHandler(int sigNum) {
    handleExit();
    exit(sigNum);
}

void SIGWINCHCallbackEventHandler(int sigNum) {
    updateDim();
}

int main (int argc, char *argv[]) {
    signal(SIGINT, SIGINTCallbackEventHandler);
    signal(SIGWINCH, SIGWINCHCallbackEventHandler);

    printf("%s", ANSI_escape_code::ENABLE_ALT_BUFFER);
    printf("%s", ANSI_escape_code::ERASE_SCREEN);
    printf("%s", ANSI_escape_code::CURSOR_INVISIBLE);

    updateDim();

    frameTimes.reserve(5000);

    float A = -M_PI_2; // Axis facing the screen (z-axis)
    float B = -M_PI_2; // Up / Down axis (y-axis)
    float C = M_PI_2 + M_PI_4; // Left / Right axis (x-axis)

    float sinA = sin(A), cosA = cos(A);
    float sinB = sin(B), cosB = cos(B);
    float sinC = sin(C), cosC = cos(C);
    trigValues = {sinA, cosA, sinB, cosB, sinC, cosC};

    float D = 0.0f;
    float E = 0.0f;
    float F = 0.0f;

    float sinD = sin(D), cosD = cos(D);
    float sinE = sin(E), cosE = cos(E);
    float sinF = sin(F), cosF = cos(F);

    rotatedLightSource = {
        cosD*cosE*lightSource.x + (cosD*sinE*sinF - sinD*cosF)*lightSource.y + (cosD*sinE*cosF + sinD*sinF)*lightSource.z,
        sinD*cosE*lightSource.x + (sinD*sinE*sinF + cosD*cosF)*lightSource.y + (sinD*sinE*cosF - cosD*sinF)*lightSource.z,
        -lightSource.x*sinE + lightSource.y*cosE*sinF + lightSource.z*cosE*cosF
    };
    normVector(rotatedLightSource);

    nextFrame = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now());
    previousFrame = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now());

    while (true) {
        PROFILE_SCOPE(frameTimes);

        A += 0.03f;
        B += 0.02f;
        C += 0.01f;
        sinA = sin(A), cosA = cos(A);
        sinB = sin(B), cosB = cos(B);
        sinC = sin(C), cosC = cos(C);
        trigValues = {sinA, cosA, sinB, cosB, sinC, cosC};

        /* Rotate Light Source */
        // D += 0.01f;
        // E += 0.01f;
        // F += 0.01f;
        // sinD = sin(D), cosD = cos(D);
        // sinE = sin(E), cosE = cos(E);
        // sinF = sin(F), cosF = cos(F);
        // rotatedLightSource = {
        //     cosD*cosE*lightSource.x + (cosD*sinE*sinF - sinD*cosF)*lightSource.y + (cosD*sinE*cosF + sinD*sinF)*lightSource.z,
        //     sinD*cosE*lightSource.x + (sinD*sinE*sinF + cosD*cosF)*lightSource.y + (sinD*sinE*cosF - cosD*sinF)*lightSource.z,
        //     -lightSource.x*sinE + lightSource.y*cosE*sinF + lightSource.z*cosE*cosF
        // };
        // normVector(rotatedLightSource);

        renderFrame(buffer, buffer_prev, cbuffer, cbuffer_prev, zbuffer, trigValues);

        if (FPS_LIMIT != 0.0f) {
            std::this_thread::sleep_until(nextFrame);

            previousFrame = nextFrame;
            nextFrame += std::chrono::microseconds(static_cast<int>(FRAME_DURATION_MICRO));
        }
    }
    
    return 0;
}
