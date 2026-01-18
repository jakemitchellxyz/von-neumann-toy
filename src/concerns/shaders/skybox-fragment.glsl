#version 450
#extension GL_EXT_scalar_block_layout : require

// Skybox fragment shader for Vulkan 1.3
// Supports HDR textures and additive blending
// Implements seamless UV wrapping with blending at the seam

layout(location = 0) in vec2 vTexCoord;

layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 1) uniform sampler2D skyboxTexture;

layout(set = 0, binding = 2, scalar) uniform FragmentUniforms
{
    bool useAdditiveBlending; // true for additive (hiptyc, PNG overlays), false for replace (milkyway/base)
    float exposure;           // Exposure multiplier for HDR values (default 1.0, increase to brighten)
};

void main()
{
    // Seamless UV wrapping with blending at the seam
    // Blend zone: 3% of texture width on each side (0.03) to match preprocessing edge blending
    // Preprocessing blends ~1.6% of width, but we use a slightly larger zone for smoother transition
    const float blendZone = 0.03;
    vec2 uv = vTexCoord;
    vec4 texColor;

    // Normalize U to [0, 1] range - GL_REPEAT will handle wrapping automatically
    float uNormalized = mod(uv.x, 1.0);
    if (uNormalized < 0.0)
        uNormalized += 1.0;

    // Calculate distance from nearest seam (either U=0 or U=1)
    float distFromSeam = min(uNormalized, 1.0 - uNormalized);

    // If we're within the blend zone, blend with the opposite side of the seam
    if (distFromSeam < blendZone)
    {
        // Sample at current position
        vec2 uvCurrent = vec2(uNormalized, uv.y);
        vec4 colorCurrent = texture(skyboxTexture, uvCurrent);

        // Sample at opposite side of seam (wrap around)
        // At U=0.01, blend with U=0.99 (opposite side of seam)
        // At U=0.99, blend with U=0.01 (opposite side of seam)
        float uOpposite;
        if (uNormalized < 0.5)
        {
            // Near left edge (U near 0): blend with right edge (U near 1)
            uOpposite = 1.0 - uNormalized;
        }
        else
        {
            // Near right edge (U near 1): blend with left edge (U near 0)
            uOpposite = 1.0 - uNormalized;
        }
        vec2 uvOpposite = vec2(uOpposite, uv.y);
        vec4 colorOpposite = texture(skyboxTexture, uvOpposite);

        // Blend factor: 0 at distFromSeam=blendZone (use current), 1 at distFromSeam=0 (use opposite)
        // Use a smoother curve (smoothstep) for more natural blending
        float blendFactor = 1.0 - (distFromSeam / blendZone);
        // Apply smoothstep twice for even smoother transition (s-curve)
        float smoothBlend = smoothstep(0.0, 1.0, smoothstep(0.0, 1.0, blendFactor));
        texColor = mix(colorCurrent, colorOpposite, smoothBlend);
    }
    else
    {
        // Middle region: normal sampling
        texColor = texture(skyboxTexture, vec2(uNormalized, uv.y));
    }

    vec3 color = texColor.rgb;
    float alpha = texColor.a;

    // Apply exposure to scale HDR values for display
    // HDR files can have values < 1.0 (dark) or > 1.0 (bright)
    // Multiply by exposure to adjust brightness to visible range
    color *= exposure;

    if (useAdditiveBlending)
    {
        // For additive blending with PNG overlays:
        // Multiply color by alpha so transparent/black pixels add nothing
        // This ensures black pixels (alpha=0 or color=black) don't darken the sky
        // Only colored pixels add their brightness to the sky beneath
        // For RGB textures (no alpha), alpha will be 1.0, so color is unchanged
        color *= alpha;
    }

    // Output the color
    // For additive blending: color is already multiplied by alpha, so black/transparent adds nothing
    // For non-additive (base HDR layer): color is output directly, alpha is 1.0
    fragColor = vec4(color, 1.0);
}
