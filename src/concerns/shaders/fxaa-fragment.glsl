#version 330 core

// FXAA (Fast Approximate Anti-Aliasing) fragment shader
// Based on NVIDIA FXAA 3.11 implementation

out vec2 vTexCoord;

uniform sampler2D uSourceTexture;
uniform vec2 uInvScreenSize; // 1.0 / screenWidth, 1.0 / screenHeight

// FXAA quality settings
#define FXAA_EDGE_THRESHOLD_MIN (1.0 / 32.0)
#define FXAA_EDGE_THRESHOLD_MAX (1.0 / 8.0)
#define FXAA_SUBPIXEL_QUALITY (1.0 / 4.0)

void main()
{
    vec2 uv = vTexCoord;

    // Sample center pixel
    vec3 rgbNW = texture(uSourceTexture, uv + vec2(-uInvScreenSize.x, -uInvScreenSize.y)).xyz;
    vec3 rgbNE = texture(uSourceTexture, uv + vec2(uInvScreenSize.x, -uInvScreenSize.y)).xyz;
    vec3 rgbSW = texture(uSourceTexture, uv + vec2(-uInvScreenSize.x, uInvScreenSize.y)).xyz;
    vec3 rgbSE = texture(uSourceTexture, uv + vec2(uInvScreenSize.x, uInvScreenSize.y)).xyz;
    vec3 rgbM = texture(uSourceTexture, uv).xyz;

    // Convert to luminance
    const vec3 luma = vec3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM = dot(rgbM, luma);

    // Find edge direction
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // Early exit if not an edge
    float lumaRange = lumaMax - lumaMin;
    if (lumaRange < max(FXAA_EDGE_THRESHOLD_MIN, lumaMax * FXAA_EDGE_THRESHOLD_MAX))
    {
        gl_FragColor = vec4(rgbM, 1.0);
        return;
    }

    // Calculate edge direction
    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce =
        max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_SUBPIXEL_QUALITY), FXAA_EDGE_THRESHOLD_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    // Increase clamp from 8.0 to 12.0 for wider sampling radius
    dir = min(vec2(12.0, 12.0), max(vec2(-12.0, -12.0), dir * rcpDirMin)) * uInvScreenSize;

    // Sample along edge with slightly wider sampling distances
    vec3 rgbA = 0.5 * (texture(uSourceTexture, uv - dir * (1.0 / 3.0 - 0.4)).xyz +
                       texture(uSourceTexture, uv + dir * (1.0 / 3.0 - 0.4)).xyz);
    vec3 rgbB =
        rgbA * 0.5 + 0.25 * (texture(uSourceTexture, uv - dir * 0.6).xyz + texture(uSourceTexture, uv + dir * 0.6).xyz);

    // Choose best sample
    float lumaB = dot(rgbB, luma);
    if ((lumaB < lumaMin) || (lumaB > lumaMax))
    {
        gl_FragColor = vec4(rgbA, 1.0);
    }
    else
    {
        gl_FragColor = vec4(rgbB, 1.0);
    }
}
