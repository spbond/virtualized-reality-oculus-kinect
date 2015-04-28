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
 
const int SIZE = 3;
const float INVALID = 0;

in vec3 vertex[SIZE];       // Incoming from vertex shader
in vec2 tex_coords[SIZE];   // Incoming from vertex shader

out vec2 uv;
//out vec3 surface_normal; // Used for virtual lighting

void main() {
    // Find 2 sides of triangle
    vec3 vector1 = vertex[1] - vertex[0];
    vec3 vector2 = vertex[2] - vertex[0];

    // Uncomment the following for virtual lighting
    //vec3 normal = cross(vector1, vector2);
    
    // Only draw triangles that do not have a "long" side 
    if( length(vector1) < .1 && length(vector2) < .1 ) {
        
        for(int i = 0; i < SIZE; ++i) {
            gl_Position = gl_in[i].gl_Position;
            //surface_normal = normal; // Used for virtual lighting
            uv = tex_coords[i];
            
            EmitVertex();
        }
    }
}
