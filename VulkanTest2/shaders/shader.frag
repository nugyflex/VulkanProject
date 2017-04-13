#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in float time;
layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) out vec4 outColor;

void main() {
	
	if (fragTexCoord == vec2(0,0) || fragTexCoord == vec2(0.111,0)) {
		outColor = vec4(fragColor.x, fragColor.y, fragColor.z, 0.5);
	}
	else{
		outColor = texture(texSampler, fragTexCoord);
	}
	//outColor = vec4(fragColor.x, fragColor.y, fragColor.z, 1);

}

