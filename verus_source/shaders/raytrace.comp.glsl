#version 460
#extension GL_EXT_ray_query : require
#extension GL_EXT_scalar_block_layout : require

// work group size
layout(local_size_x = 16, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, set = 0, scalar) buffer storageBuffer
{
	vec3 imageData[];
};
layout(binding = 1, set = 0) uniform accelerationStructureEXT tlas;
layout(binding = 2, set = 0, scalar) buffer Vertices
{
	vec3 vertices[];
};
layout(binding = 3, set = 0, scalar) buffer Indices
{
	uint indices[];
};

struct HitRecord
{
	vec3 color;
	vec3 position;
	vec3 normal;
};

HitRecord getObjectHitRecord(rayQueryEXT rayQuery)
{
	HitRecord record;

	// get the primitive ID of the intersection
	const int primitiveID = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);

	// get the indices of the triangle
	const uint i0 = indices[3 * primitiveID + 0];
	const uint i1 = indices[3 * primitiveID + 1];
	const uint i2 = indices[3 * primitiveID + 2];

	// get the vertices of the triangle
	const vec3 v0 = vertices[i0];
	const vec3 v1 = vertices[i1];
	const vec3 v2 = vertices[i2];
		
	// get the barycentric coordinates of the intersection
	vec3 barycentrics = vec3(0.0, rayQueryGetIntersectionBarycentricsEXT(rayQuery, true));
	barycentrics.x = 1.0 - barycentrics.y - barycentrics.z;

	vec3 triangleNormal = normalize(cross(v1 - v0, v2 - v0));
	vec3 triangleIntersectionPoint = barycentrics.x * v0 + barycentrics.y * v1 + barycentrics.z * v2;

	record.position = triangleIntersectionPoint;
	record.normal = triangleNormal;
	record.color = vec3(0.7f);

	return record;
}

vec3 skyColor(vec3 direction) 
{
	if(direction.y > 0.0f)
	{
		return mix(vec3(1.0f), vec3(0.25f, 0.5f, 0.1f), direction.y);
	}
	else
	{
		return vec3(0.03f);
	}
}

void main()
{
	const uvec2 resolution = uvec2(1920, 1280);

	// Get current pixel coordinates.
	const uvec2 pixel = gl_GlobalInvocationID.xy; 

	if(pixel.x >= resolution.x || pixel.y >= resolution.y) 
		return; // check if pixel is outside of the image


	// Calculate the ray direction.
	const vec3 cameraOrigin = vec3(-0.001, 1, 6);
	vec3 rayOrigin          = cameraOrigin;
	
	const vec2 screenUV = vec2(
		(2.0 * float(pixel.x) + 1.0 - resolution.x) / resolution.y,
		-(2.0 * float(pixel.y) + 1.0 - resolution.y) / resolution.y // flip y-axis
		);
	
	const float fovVerticalSlope = 1.0 / 5.0;
	vec3 rayDirection            = vec3(fovVerticalSlope * screenUV.x, fovVerticalSlope * screenUV.y, -1.0);

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

	vec3 pixelColor;

	// check if the ray hit something
	if(rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT)
	{
		HitRecord record = getObjectHitRecord(rayQuery);


		pixelColor = vec3(0.5) + 0.5 * record.normal;
	}
	else 
	{
		pixelColor = vec3(0.0, 0.0, 1.0);
	}


	// get the t-value of the committed intersection
	const float t = rayQueryGetIntersectionTEXT(rayQuery, true); // true means that the t-value is returned

	// buffer is one-dimensional, so we need to calculate the linear index
	uint linearIndex = resolution.x * pixel.y + pixel.x;

	imageData[linearIndex] = pixelColor;
}
