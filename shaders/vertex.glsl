#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

uniform float uMinY;
uniform float uMaxY;
uniform bool uIsAxis;

out vec3 vColor;

void main() {
    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);

    if (uIsAxis){
        // X= rojo Y= verde Z= azul si es eje cordenado
        vColor = aColor;
    } else{
        // mapa de colores azul (bajo), amarillo (medio), rojo (alto)
        float range = uMaxY - uMinY;
        float t = (range > 0.00001) ? clamp((aPos.y - uMinY) / range, 0.0, 1.0) : 0.5;

        vec3 blue   = vec3(0.0, 0.0, 1.0);
        vec3 yellow = vec3(1.0, 1.0, 0.0);
        vec3 red    = vec3(1.0, 0.0, 0.0);

        if (t < 0.5){
            vColor = mix(blue, yellow, t * 2.0);
        }else{
            vColor = mix(yellow, red, (t - 0.5) * 2.0);
        }
    }
}