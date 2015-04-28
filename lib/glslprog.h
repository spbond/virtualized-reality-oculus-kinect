// glslprog.h
// Glenn G. Chappell
// 26 Sep 2012
// Based on code by Orion Lawlor
// Public Domain
//
// For CS 381 Fall 2012
// Header for shader-handling utilities
// There is no associated source file
// Requires GLUT, GLEW
//
// Before including this file, you may not include any GLUT or OpenGL
// headers without including glew.h first.

#ifndef FILE_GLSLPROG_H_INCLUDED
#define FILE_GLSLPROG_H_INCLUDED

// OpenGL/GLUT includes - DO THESE FIRST
#include <cstdlib>       // Do this before GL/GLUT includes
using std::exit;
#ifndef __APPLE__
# include <GL/glew.h>
# include <GL/glut.h>    // GLUT stuff, includes OpenGL headers as well
#else
# include <GLEW/glew.h>
# include <GLUT/glut.h>  // Apple puts glut.h in a different place
#endif

// Other includes
#include <cctype>        // For std::isspace
#include <fstream>       // For std::ifstream
#include <string>        // For string, std::getline
#include <iostream>      // For cout, std::cin, endl;
#include <cstdlib>       // For std::exit, std::size_t
using std::string;
using std::cout;
using std::endl;

// To create & use a shader program object:
//   o If your GLSL code is stored in text files.
//
//     GLuint prog = makeShaderProgramFromFiles(VSHADER_FILENAME,
//                                              FSHADER_FILENAME);
//     glUseProgram(prog);
//
// -- OR --
//
//   o If your GLSL code is stored in strings (hard-coded in your C++
//      source?).
//
//     GLuint prog = makeShaderProgram(VSHADER_SOURCE_STRING,
//                                     FSHADER_SOURCE_STRING);
//     glUseProgram(prog);
//
// Multiple shader program objects may be created by making multiple
//  calls to makeProgramObject or makeProgramObjectFromFiles, and
//  storing the return values in different GLhandleARB variables.

// You may also wish to use function getShaderFilenames to read
//  shader filenames from the command line or stdin.


// glslProgErrorExit
// Output given string to cout, followed by newline, then wait for
//  keypress, and do exit(1).
inline void glslProgErrorExit(const string& msg)
{
    cout << msg << endl << endl;
    cout << "Press ENTER to quit ";
    while (std::cin.get() != '\n') ;
    std::exit(1);
}


// checkShaderOp
// Print message and exit if GLSL build error.
inline void
checkShaderOp(GLhandleARB obj, GLenum errType, const string& where)
{
    GLint compiled;
    glGetObjectParameterivARB(obj, errType, &compiled);
    if (!compiled)
    {
        const GLsizei LOGSIZE = 10000;
        GLchar errorLog[LOGSIZE];
        // We need to consider the possibility of buffer overflow here.
        //  Having considered it, we note that OpenGL knows how big our
        //  buffer is, and there is no possibility of overflow.
        GLsizei len = 0;
        glGetInfoLogARB(obj, LOGSIZE, &len, errorLog);
        glslProgErrorExit("ERROR - Could not build GLSL shader: "
                + where + "\n\nERROR LOG:\n" + errorLog);
    }
}


// makeShaderObject
// Create a vertex or fragment shader from given source code.
// Prints message and exits on GLSL compile error.
inline GLuint
makeShaderObject(GLenum target, const string& sourceCode)
{
    GLuint h = glCreateShader(target);
    const GLchar* sourceCodePtr = sourceCode.c_str();
    glShaderSource(h, GLsizei(1), &sourceCodePtr, NULL);
    glCompileShader(h);
    checkShaderOp(h, GL_OBJECT_COMPILE_STATUS_ARB, "compile\n" + sourceCode);
    return h;
}


// makeProgramObject
// Create complete program object from these chunks of GLSL shader code.
// Prints message and exits on GLSL build error, or if GLSL unavailable.
// To USE this program object, do glUseProgram(RETURN_VALUE);
//
// *****************************************************************
// ** THIS IS THE FUNCTION TO CALL IF YOUR GLSL CODE IS STORED IN **
// ** STRINGS (HARD-CODED IN YOUR C++ SOURCE?).                   **
// *****************************************************************
//
inline GLuint
makeShaderProgram(const string& vShaderSource, const string& fShaderSource)
{
    if (glUseProgram == 0)
    { // glew never set up, or OpenGL is too old.
        glslProgErrorExit( "ERROR - GLSL not available\n"
                           "(Hardware/software too old? glewInit not called?)");
    }

    GLuint p = glCreateProgram();
    glAttachShader(p, makeShaderObject(GL_VERTEX_SHADER_ARB,   vShaderSource));
    glAttachShader(p, makeShaderObject(GL_FRAGMENT_SHADER_ARB, fShaderSource));
    glLinkProgram(p);
    checkShaderOp(p, GL_OBJECT_LINK_STATUS_ARB, "link");
    return p;
}


// makeProgramObject
// Create complete program object from these chunks of GLSL shader code.
// Prints message and exits on GLSL build error, or if GLSL unavailable.
// To USE this program object, do glUseProgram(RETURN_VALUE);
//
// *****************************************************************
// ** THIS IS THE FUNCTION TO CALL IF YOUR GLSL CODE IS STORED IN **
// ** STRINGS (HARD-CODED IN YOUR C++ SOURCE?).                   **
// *****************************************************************
//
inline GLuint
makeShaderProgram(const string& vShaderSource, const string& gShaderSource,
                  const string& fShaderSource)
{
    if (glUseProgram == 0)
    { // glew never set up, or OpenGL is too old.
        glslProgErrorExit( "ERROR - GLSL not available\n"
                           "(Hardware/software too old? glewInit not called?)");
    }

    GLuint p = glCreateProgram();
    glAttachShader(p, makeShaderObject(GL_VERTEX_SHADER,   vShaderSource));
    glAttachShader(p, makeShaderObject(GL_GEOMETRY_SHADER, gShaderSource));
    glAttachShader(p, makeShaderObject(GL_FRAGMENT_SHADER, fShaderSource));
    glLinkProgram(p);
    checkShaderOp(p, GL_OBJECT_LINK_STATUS_ARB, "link");
    return p;
}


// readFileIntoString
// Read entire contents of file with given name into given string.
// Returns true on success, false if read error.
inline bool
readFileIntoString(const string& fName, string& contents)
{
    contents.clear();
    std::ifstream file(fName.c_str());
    if (!file)
    {
        return false;
    }
    else while (true)
    {
        char chr = char(file.get());
        if (!file)
        {
            return file.eof();
        }
        contents += chr;
    }
    return false;
}


// makeProgramObjectFromFiles
// Create a complete shader object from these GLSL files.
// Prints message and exits on file read error, GLSL build error,
//  or if GLSL unavailable.
// To USE this program object, do glUseProgram(RETURN_VALUE);
//
// *****************************************************************
// ** THIS IS THE FUNCTION TO CALL IF YOUR GLSL CODE IS STORED IN **
// ** TEXT FILES.                                                 **
// *****************************************************************
//
inline GLuint
makeShaderProgramFromFiles( const string& vFilename, const string& fFilename )
{
    string vShaderSource;
    if (!readFileIntoString(vFilename, vShaderSource))
    {
        glslProgErrorExit( "ERROR - Could not read vertex shader code file: "
                           + vFilename);
    }

    string fShaderSource;
    if (!readFileIntoString(fFilename, fShaderSource))
    {
        glslProgErrorExit( "ERROR - Could not read fragment shader code file: "
                           + fFilename);
    }

    return makeShaderProgram(vShaderSource, fShaderSource);
}


// makeProgramObjectFromFiles
// Create a complete shader object from these GLSL files.
// Prints message and exits on file read error, GLSL build error,
//  or if GLSL unavailable.
// To USE this program object, do glUseProgram(RETURN_VALUE);
//
// *****************************************************************
// ** THIS IS THE FUNCTION TO CALL IF YOUR GLSL CODE IS STORED IN **
// ** TEXT FILES.                                                 **
// *****************************************************************
//
inline GLuint
makeShaderProgramFromFiles( const string& vFilename, const string& gFilename,
                            const string& fFilename )
{
    string vShaderSource;
    if (!readFileIntoString(vFilename, vShaderSource))
    {
        glslProgErrorExit( "ERROR - Could not read vertex shader code file: "
                           + vFilename);
    }

    string gShaderSource;
    if (!readFileIntoString(gFilename, gShaderSource))
    {
        glslProgErrorExit( "ERROR - Could not read geometry shader code file: "
                           + gFilename);
    }

    string fShaderSource;
    if (!readFileIntoString(fFilename, fShaderSource))
    {
        glslProgErrorExit( "ERROR - Could not read fragment shader code file: "
                           + fFilename);
    }

    return makeShaderProgram(vShaderSource, gShaderSource, fShaderSource);
}


// getShaderFilenames
// Gets base for shader source filenames from command line or cin, as
//  appropriate. Sets vFile and fFile to the appropriate filenames.
//  Command-line processing is optional. To do such processing, pass the
//  arguments received by function main; leave them off to disable it.
inline void
getShaderFilenames(string& vFile, string& fFile, int argc=0, char ** argv=0)
{
    string fnameBase;  // Base for shader filenames

    // Try to get shader filename base from the command line?
    if (argc > 1)
    {
        fnameBase = argv[1];
    }

    // Iterate until we have a good shader filename base
    // Read one from stdin if we do not have one yet
    while (true)
    {
        // Elim leading space, trailing space & "_" in fnameBase
        while (!fnameBase.empty())
        {
            if (!std::isspace(fnameBase[0]))
                break;
            fnameBase = fnameBase.substr(1);
        }
        while (!fnameBase.empty())
        {
            std::size_t s = fnameBase.size();
            char c = fnameBase[s-1];
            if (!isspace(c) && c != '_')
                break;
            fnameBase.resize(s-1);
        }

        // Did we get a nonempty base name?
        if (!fnameBase.empty())
            break;

        // If not, then input one from stdin
        cout << endl;
        cout << "Enter the base for shader source filenames."
             << endl;
        cout << endl;
        cout << "For example, if you enter `abc', then shaders "
             << "will be read from" << endl;
        cout << "`abc_v.glsl' and `abc_f.glsl'." << endl;
        cout << endl;
        cout << "===> ";  // Prompt
        getline(std::cin, fnameBase);
    }

    // We have a good shader filename base; set shader source filenames
    vFile = fnameBase + "_v.glsl";
    fFile = fnameBase + "_f.glsl";
}


#endif //#ifndef FILE_GLSL_PROG_H_INCLUDED

