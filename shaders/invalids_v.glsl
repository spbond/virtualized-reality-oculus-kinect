/* Author: Shaun Bond (samuraicodemonkey@gmail.com)
 * Date:   4-20-2015
 *
 * This code is licensed to you under the terms of the Apache License, version
 * 2.0, or, at your option, the terms of the GNU General Public License,
 * version 2.0. See the APACHE20 and GPL2 files for the text of the licenses,
 * or the following URLs:
 * http://www.apache.org/licenses/LICENSE-2.0
 * http://www.gnu.org/licenses/gpl-2.0.txt
 *
 * If you redistribute this file in source form, modified or unmodified, you
 * may:
 *   1) Leave this header intact and distribute it under the same terms,
 *      accompanying it with the APACHE20 and GPL20 files, or
 *   2) Delete the Apache 2.0 clause and accompany it with the GPL2 file, or
 *   3) Delete the GPL v2 clause and accompany it with the APACHE20 file
 * In all cases you must keep the copyright notice intact and include a copy
 * of the CONTRIB file.
 *
 * Binary distributions must follow the binary distribution requirements of
 * either License.
 */
 
// So I can use things like gl_ModelViewProjectionMatrix to save time/effort.
// Ideally, the shaders should use modern glsl specification.
#version 150

//out vec4 v_color;
out vec3 vertex;
out vec2 tex_coords;

void main() {
    vertex = gl_Vertex.xyz; // Unmodified coordinates passed to geometry shader

    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;

    tex_coords = gl_MultiTexCoord0.st;
}