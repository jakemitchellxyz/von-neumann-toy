#version 120

// Skybox vertex shader
// Works with immediate mode (glBegin/glEnd)
// Uses built-in OpenGL variables for compatibility

varying vec2 vTexCoord;

void main()
{
    // gl_MultiTexCoord0 is set by glTexCoord2f calls
    vTexCoord = gl_MultiTexCoord0.st;
    
    // gl_Vertex is set by glVertex3f calls
    // gl_ModelViewProjectionMatrix is the combined MVP matrix
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
