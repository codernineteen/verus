#version 460
#extension GL_EXT_ray_query : require
#extension GL_EXT_scalar_block_layout : enable

// work group size
layout(local_size_x = 16, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, set = 0, scalar) buffer storageBuffer
{
	vec3 imageData[];
};
layout(binding = 1, set = 0) uniform accelerationStructureEXT tlas;

void main()
{
	const uvec2 resolution = uvec2(800, 600);

	// Get current pixel coordinates.
	const uvec2 pixel = gl_GlobalInvocationID.xy; 
	if(pixel.x >= resolution.x || pixel.y >= resolution.y) 
		return; // check if pixel is outside of the image


	// Calculate the ray direction.
	const vec3 cameraOrigin = vec3(-0.001, 1, 6);
	vec3 rayOrigin = cameraOrigin;
	
	const vec2 screenUV = vec2(
		(2.0 * float(pixel.x) + 1.0 - resolution.x) / resolution.y,
		-(2.0 * float(pixel.y) + 1.0 - resolution.y) / resolution.y // flip y-axis
		);
	
	const float fovVerticalSlope = 1.0 / 5.0;
	vec3 rayDirection = vec3(fovVerticalSlope * screenUV.x, fovVerticalSlope * screenUV.y, -1.0);

	rayQueryEXT rayQuery;
	rayQueryInitializeEXT(
		rayQuery,
		tlas,
		gl_RayFlagsOpaqueEXT,
		0xFF,
		rayOrigin,
		0.0,
		rayDirection,
		10000.0
	);

	// update committed intersection
	while(rayQueryProceedEXT(rayQuery))
	{
		
	}
	// get the t-value of the committed intersection
	const float t = rayQueryGetIntersectionTEXT(rayQuery, true); // true means that the t-value is returned

	// buffer is one-dimensional, so we need to calculate the linear index
	uint linearIndex = resolution.x * pixel.y + pixel.x;

	imageData[linearIndex] = vec3(t / 10.0);
}