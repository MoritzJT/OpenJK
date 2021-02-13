/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
#include "tr_local.h"
#include "tr_allocator.h"
#include "glext.h"
#include <algorithm>

backEndData_t	*backEndData;
backEndState_t	backEnd;


static float	s_flipMatrix[16] = {
	// convert from our coordinate system (looking down X)
	// to OpenGL's coordinate system (looking down -Z)
	0, 0, -1, 0,
	-1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 0, 1
};


/*
** GL_Bind
*/
void GL_Bind( image_t *image ) {
	int texnum;

	if ( !image ) {
		ri.Printf( PRINT_WARNING, "GL_Bind: NULL image\n" );
		texnum = tr.defaultImage->texnum;
	} else {
		texnum = image->texnum;
	}

	if ( r_nobind->integer && tr.dlightImage ) {		// performance evaluation option
		texnum = tr.dlightImage->texnum;
	}

	if (glState.currenttextures[glState.currenttmu] != texnum) {
		if (image) {
			image->frameUsed = tr.frameCount;
		}
		glState.currenttextures[glState.currenttmu] = texnum;
		if (image)
		{
			if (image->flags & IMGFLAG_CUBEMAP)
				qglBindTexture(GL_TEXTURE_CUBE_MAP, texnum);
			else if (image->flags & IMGFLAG_3D)
				qglBindTexture(GL_TEXTURE_3D, texnum);
			else if (image->flags & IMGFLAG_MULTISAMPLED)
				qglBindTexture(GL_TEXTURE_2D_MULTISAMPLE, texnum);
			else
				qglBindTexture(GL_TEXTURE_2D, texnum);
		}
		else
			qglBindTexture(GL_TEXTURE_2D, texnum);
	}
}

/*
** GL_SelectTexture
*/
void GL_SelectTexture( int unit )
{
	if ( glState.currenttmu == unit )
	{
		return;
	}

	if (!(unit >= 0 && unit <= 31))
		ri.Error( ERR_DROP, "GL_SelectTexture: unit = %i", unit );

	qglActiveTexture( GL_TEXTURE0 + unit );

	glState.currenttmu = unit;
}

/*
** GL_BindToTMU
*/
void GL_BindToTMU( image_t *image, int tmu )
{
	int		texnum;
	//int     oldtmu = glState.currenttmu;

	if (!image)
		texnum = 0;
	else
		texnum = image->texnum;

	if ( glState.currenttextures[tmu] != texnum ) {
		GL_SelectTexture( tmu );
		if (image)
			image->frameUsed = tr.frameCount;
		glState.currenttextures[tmu] = texnum;

		if (image) 
		{
			if (image->flags & IMGFLAG_CUBEMAP)
				qglBindTexture(GL_TEXTURE_CUBE_MAP, texnum);
			else if (image->flags & IMGFLAG_3D)
				qglBindTexture(GL_TEXTURE_3D, texnum);
			else
				qglBindTexture(GL_TEXTURE_2D, texnum);
		}
		else
			qglBindTexture( GL_TEXTURE_2D, texnum );
		//GL_SelectTexture( oldtmu );
	}
}

/*
** GL_Cull
*/
void GL_Cull( int cullType ) {
	if ( glState.faceCulling == cullType ) {
		return;
	}

	if ( backEnd.projection2D )
		cullType = CT_TWO_SIDED;

	if ( cullType == CT_TWO_SIDED ) 
	{
		if ( glState.faceCulling != CT_TWO_SIDED )
			qglDisable( GL_CULL_FACE );
	} 
	else 
	{
		qboolean cullFront = (qboolean)(cullType == CT_FRONT_SIDED);
		
		if ( glState.faceCulling == CT_TWO_SIDED )
			qglEnable( GL_CULL_FACE );

		qglCullFace( cullFront ? GL_FRONT : GL_BACK);
	}

	glState.faceCulling = cullType;
}

void GL_DepthRange( float min, float max )
{
	if ( glState.minDepth == min && glState.maxDepth == max )
	{
		return;
	}

	qglDepthRange(min, max);
	glState.minDepth = min;
	glState.maxDepth = max;
}

/*
** GL_State
**
** This routine is responsible for setting the most commonly changed state
** in Q3.
*/
void GL_State( uint32_t stateBits )
{
	uint32_t diff = stateBits ^ glState.glStateBits;

	if ( !diff )
	{
		return;
	}

	//
	// check depthFunc bits
	//
	if ( diff & GLS_DEPTHFUNC_BITS )
	{
		if ( stateBits & GLS_DEPTHFUNC_EQUAL )
		{
			qglDepthFunc( GL_EQUAL );
		}
		else if ( stateBits & GLS_DEPTHFUNC_GREATER)
		{
			qglDepthFunc( GL_GREATER );
		}
		else if (stateBits & GLS_DEPTHFUNC_LESS)
		{
			qglDepthFunc( GL_LESS );
		}
		else
		{
			qglDepthFunc( GL_LEQUAL );
		}
	}

	//
	// check blend bits
	//
	if ( diff & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) )
	{
		GLenum srcFactor = GL_ONE, dstFactor = GL_ONE;

		if ( stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) )
		{
			switch ( stateBits & GLS_SRCBLEND_BITS )
			{
			case GLS_SRCBLEND_ZERO:
				srcFactor = GL_ZERO;
				break;
			case GLS_SRCBLEND_ONE:
				srcFactor = GL_ONE;
				break;
			case GLS_SRCBLEND_DST_COLOR:
				srcFactor = GL_DST_COLOR;
				break;
			case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
				srcFactor = GL_ONE_MINUS_DST_COLOR;
				break;
			case GLS_SRCBLEND_SRC_ALPHA:
				srcFactor = GL_SRC_ALPHA;
				break;
			case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
				srcFactor = GL_ONE_MINUS_SRC_ALPHA;
				break;
			case GLS_SRCBLEND_DST_ALPHA:
				srcFactor = GL_DST_ALPHA;
				break;
			case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
				srcFactor = GL_ONE_MINUS_DST_ALPHA;
				break;
			case GLS_SRCBLEND_ALPHA_SATURATE:
				srcFactor = GL_SRC_ALPHA_SATURATE;
				break;
			default:
				ri.Error( ERR_DROP, "GL_State: invalid src blend state bits" );
				break;
			}

			switch ( stateBits & GLS_DSTBLEND_BITS )
			{
			case GLS_DSTBLEND_ZERO:
				dstFactor = GL_ZERO;
				break;
			case GLS_DSTBLEND_ONE:
				dstFactor = GL_ONE;
				break;
			case GLS_DSTBLEND_SRC_COLOR:
				dstFactor = GL_SRC_COLOR;
				break;
			case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
				dstFactor = GL_ONE_MINUS_SRC_COLOR;
				break;
			case GLS_DSTBLEND_SRC_ALPHA:
				dstFactor = GL_SRC_ALPHA;
				break;
			case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
				dstFactor = GL_ONE_MINUS_SRC_ALPHA;
				break;
			case GLS_DSTBLEND_DST_ALPHA:
				dstFactor = GL_DST_ALPHA;
				break;
			case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
				dstFactor = GL_ONE_MINUS_DST_ALPHA;
				break;
			default:
				ri.Error( ERR_DROP, "GL_State: invalid dst blend state bits" );
				break;
			}
			if (glState.blend == qfalse)
			{
				qglEnable(GL_BLEND);
				glState.blend = qtrue;
			}
			qglBlendFunc( srcFactor, dstFactor );
		}
		else
		{
			if (glState.blend == qtrue)
			{
				glState.blend = qfalse;
				qglDisable(GL_BLEND);
			}
		}
	}

	//
	// check depthmask
	//
	if ( diff & GLS_DEPTHMASK_TRUE )
	{
		if ( stateBits & GLS_DEPTHMASK_TRUE )
		{
			qglDepthMask( GL_TRUE );
		}
		else
		{
			qglDepthMask( GL_FALSE );
		}
	}

	//
	// fill/line mode
	//
	if ( diff & GLS_POLYMODE_LINE )
	{
		if ( stateBits & GLS_POLYMODE_LINE )
		{
			qglPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
		}
		else
		{
			qglPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
		}
	}

	//
	// depthtest
	//
	if ( diff & GLS_DEPTHTEST_DISABLE )
	{
		if ( stateBits & GLS_DEPTHTEST_DISABLE )
		{
			qglDisable( GL_DEPTH_TEST );
		}
		else
		{
			qglEnable( GL_DEPTH_TEST );
		}
	}

	if ( diff & GLS_POLYGON_OFFSET_FILL )
	{
		if ( stateBits & GLS_POLYGON_OFFSET_FILL )
		{
			qglEnable( GL_POLYGON_OFFSET_FILL );
			qglPolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
		}
		else
		{
			qglDisable( GL_POLYGON_OFFSET_FILL );
		}
	}

	glState.glStateBits = stateBits;
}

void GL_VertexAttribPointers(
		size_t numAttributes,
		vertexAttribute_t *attributes )
{
	assert(attributes != nullptr || numAttributes == 0);

	uint32_t newAttribs = 0;
	for ( int i = 0; i < numAttributes; i++ )
	{
		vertexAttribute_t& attrib = attributes[i];

		newAttribs |= (1 << attrib.index);
		if ( memcmp(&glState.currentVaoAttribs[attrib.index], &attrib,
					sizeof(glState.currentVaoAttribs[attrib.index])) == 0 )
		{
			// No change
			continue;
		}

		R_BindVBO(attrib.vbo);
		if ( attrib.integerAttribute )
		{
			qglVertexAttribIPointer(attrib.index,
				attrib.numComponents,
				attrib.type,
				attrib.stride,
				BUFFER_OFFSET(attrib.offset));
		}
		else
		{
			qglVertexAttribPointer(attrib.index,
				attrib.numComponents,
				attrib.type,
				attrib.normalize,
				attrib.stride,
				BUFFER_OFFSET(attrib.offset));
		}

		if (attrib.stepRate != glState.attrStepRate)
		{
			glState.attrIndex = attrib.index;
			glState.attrStepRate = attrib.stepRate;
			qglVertexAttribDivisor(attrib.index, attrib.stepRate);
		}

		glState.currentVaoAttribs[attrib.index] = attrib;
	}

	uint32_t diff = newAttribs ^ glState.vertexAttribsState;
	if ( diff )
	{
		for ( int i = 0, j = 1; i < ATTR_INDEX_MAX; i++, j <<= 1 )
		{
			// FIXME: Use BitScanForward?
			if (diff & j)
			{
				if(newAttribs & j)
					qglEnableVertexAttribArray(i);
				else
					qglDisableVertexAttribArray(i);
			}
		}

		glState.vertexAttribsState = newAttribs;
	}
}

void GL_DrawIndexed(
		GLenum primitiveType,
		int numIndices,
		GLenum indexType,
		int offset,
		int numInstances,
		int baseVertex)
{
	assert(numInstances > 0);
	qglDrawElementsInstancedBaseVertex(
			primitiveType,
			numIndices,
			indexType,
			BUFFER_OFFSET(offset),
			numInstances,
			baseVertex);
}

void GL_MultiDrawIndexed(
		GLenum primitiveType,
		int *numIndices,
		glIndex_t **offsets,
		int numDraws)
{
	assert(numDraws > 0);
	qglMultiDrawElements(
			primitiveType,
			numIndices,
			GL_INDEX_TYPE,
			(const GLvoid **)offsets,
			numDraws);
}

void GL_Draw(GLenum primitiveType, int firstVertex, int numVertices, int numInstances)
{
	assert(numInstances > 0);
	qglDrawArraysInstanced(primitiveType, firstVertex, numVertices, numInstances);
}

void GL_SetProjectionMatrix(matrix_t matrix)
{
	Matrix16Copy(matrix, glState.projection);
	Matrix16Multiply(glState.projection, glState.modelview, glState.modelviewProjection);	
}


void GL_SetModelviewMatrix(matrix_t matrix)
{
	Matrix16Copy(matrix, glState.modelview);
	Matrix16Multiply(glState.projection, glState.modelview, glState.modelviewProjection);	
}


/*
================
RB_Hyperspace

A player has predicted a teleport, but hasn't arrived yet
================
*/
static void RB_Hyperspace( void ) {
	float c = ( backEnd.refdef.time & 255 ) / 255.0f;
	vec4_t v = { c, c, c, 1.0f };
	qglClearBufferfv( GL_COLOR, 0, v );
}


static void SetViewportAndScissor( void ) {
	GL_SetProjectionMatrix( backEnd.viewParms.projectionMatrix );

	// set the window clipping
	qglViewport( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY, 
		backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );

	if ( !backEnd.viewParms.scissorX && !backEnd.viewParms.scissorY &&
			!backEnd.viewParms.scissorWidth && !backEnd.viewParms.scissorHeight )
	{
		qglScissor( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY, 
			backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );
	}
	else
	{
		qglScissor( backEnd.viewParms.scissorX, backEnd.viewParms.scissorY, 
			backEnd.viewParms.scissorWidth, backEnd.viewParms.scissorHeight );
	}
}

/*
=================
RB_BeginDrawingView

Any mirrored or portaled views have already been drawn, so prepare
to actually render the visible surfaces for this view
=================
*/
void RB_BeginDrawingView (void) {
	int clearBits = 0;

	// we will need to change the projection matrix before drawing
	// 2D images again
	backEnd.projection2D = qfalse;

	// clear content of the pre buffers
	if (r_ssr->integer)
	{
		FBO_Bind(tr.preBuffersFbo);
		qglClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		qglClear(GL_COLOR_BUFFER_BIT);
	}

	// FIXME: HUGE HACK: render to the screen fbo if we've already postprocessed the frame and aren't drawing more world
	// drawing more world check is in case of double renders, such as skyportals
	if (backEnd.viewParms.targetFbo == NULL)
	{
		if (!tr.renderFbo || (backEnd.framePostProcessed && (backEnd.refdef.rdflags & RDF_NOWORLDMODEL)))
		{
			FBO_Bind(NULL);
		}
		else
		{
			FBO_Bind(tr.renderFbo);
		}
	}
	else
	{
		FBO_Bind(backEnd.viewParms.targetFbo);

		// FIXME: hack for cubemap testing
		if (tr.renderCubeFbo != NULL && backEnd.viewParms.targetFbo == tr.renderCubeFbo)
		{
			image_t *cubemap = backEnd.viewParms.cubemap->image;

			if (r_cubeMapping->integer > 1)
				cubemap = tr.renderCubeImage;

			qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + backEnd.viewParms.targetFboLayer, cubemap->texnum, 0);
		}
		else if (tr.shadowCubeFbo != NULL && backEnd.viewParms.targetFbo == tr.shadowCubeFbo)
		{
			cubemap_t *cubemap = backEnd.viewParms.cubemap;
			qglFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, cubemap->image->texnum, 0);
		}
	}
	
	//
	// set the modelview matrix for the viewer
	//
	SetViewportAndScissor();

	// ensures that depth writes are enabled for the depth clear
	GL_State( GLS_DEFAULT );
	// clear relevant buffers
	clearBits = GL_DEPTH_BUFFER_BIT;

	if ( r_clear->integer )
	{
		clearBits |= GL_COLOR_BUFFER_BIT;
	}

	if ( r_measureOverdraw->integer || r_shadows->integer == 2 )
	{
		clearBits |= GL_STENCIL_BUFFER_BIT;
	}

	if (skyboxportal)
	{
		if (backEnd.refdef.rdflags & RDF_SKYBOXPORTAL)
		{
			if (r_fastsky->integer || (backEnd.refdef.rdflags & RDF_NOWORLDMODEL))
			{
				clearBits |= GL_COLOR_BUFFER_BIT;
				if (tr.world && tr.world->globalFog)
				{
					const fog_t		*fog = tr.world->globalFog;
					qglClearColor(fog->parms.color[0], fog->parms.color[1], fog->parms.color[2], 1.0f);
				}
				else
				{
					qglClearColor(0.3f, 0.3f, 0.3f, 1.0);
				}
			}
		}
	}
	else if ( r_fastsky->integer && !( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) )
	{
		if (tr.world && tr.world->globalFog)
		{
			const fog_t		*fog = tr.world->globalFog;
			qglClearColor(fog->parms.color[0], fog->parms.color[1], fog->parms.color[2], 1.0f);
		}
		else
		{
			qglClearColor(0.3f, 0.3f, 0.3f, 1);	// FIXME: get color of sky
		}
		clearBits |= GL_COLOR_BUFFER_BIT; // FIXME: only if sky shaders have been used
	}
	
	if (!(backEnd.refdef.rdflags & RDF_NOWORLDMODEL) && (r_dynamicGlow->integer))
	{
		if (tr.world && tr.world->globalFog )
		{ //this is because of a bug in multiple scenes I think, it needs to clear for the second scene but it doesn't normally.
			const fog_t		*fog = tr.world->globalFog;

			qglClearColor(fog->parms.color[0], fog->parms.color[1], fog->parms.color[2], 1.0f);
			clearBits |= GL_COLOR_BUFFER_BIT;
		}
	}

	// clear to white for shadow maps
	if (backEnd.viewParms.flags & VPF_SHADOWMAP)
	{
		clearBits |= GL_COLOR_BUFFER_BIT;
		qglClearColor( 1.0f, 1.0f, 1.0f, 1.0f );
	}

	// clear to black for cube maps
	if (tr.renderCubeFbo != NULL && backEnd.viewParms.targetFbo == tr.renderCubeFbo)
	{
		clearBits |= GL_COLOR_BUFFER_BIT;
		if (tr.world && tr.world->globalFog)
		{
			const fog_t		*fog = tr.world->globalFog;
			qglClearColor(fog->parms.color[0], fog->parms.color[1], fog->parms.color[2], 1.0f);
		}
		else
		{
			qglClearColor(0.3f, 0.3f, 0.3f, 1.0);
		}
	}

	qglClear(clearBits);

	if (backEnd.viewParms.targetFbo == NULL)
	{
		// Clear the glow target
		float black[] = {0.0f, 0.0f, 0.0f, 1.0f};
		qglClearBufferfv (GL_COLOR, 1, black);
	}

	if ( ( backEnd.refdef.rdflags & RDF_HYPERSPACE ) )
	{
		RB_Hyperspace();
		return;
	}

	// we will only draw a sun if there was sky rendered in this view
	backEnd.skyRenderedThisView = qfalse;

	// clip to the plane of the portal
	if ( backEnd.viewParms.isPortal ) {
#if 0
		float	plane[4];
		double	plane2[4];

		plane[0] = backEnd.viewParms.portalPlane.normal[0];
		plane[1] = backEnd.viewParms.portalPlane.normal[1];
		plane[2] = backEnd.viewParms.portalPlane.normal[2];
		plane[3] = backEnd.viewParms.portalPlane.dist;

		plane2[0] = DotProduct (backEnd.viewParms.ori.axis[0], plane);
		plane2[1] = DotProduct (backEnd.viewParms.ori.axis[1], plane);
		plane2[2] = DotProduct (backEnd.viewParms.ori.axis[2], plane);
		plane2[3] = DotProduct (plane, backEnd.viewParms.ori.origin) - plane[3];
#endif
		GL_SetModelviewMatrix( s_flipMatrix );
	}
}


#define	MAC_EVENT_PUMP_MSEC		5

UniformDataWriter::UniformDataWriter()
	: failed(false)
	, shaderProgram(nullptr)
	, scratch(scratchBuffer, sizeof(scratchBuffer), 1)
{
}

void UniformDataWriter::Start( shaderProgram_t *sp )
{
	shaderProgram = sp;
}

UniformDataWriter& UniformDataWriter::SetUniformInt( uniform_t uniform, int value )
{
	if ( shaderProgram->uniforms[uniform] == -1 )
		return *this;

	void *memory = scratch.Alloc(sizeof(UniformData) + sizeof(int));
	if ( !memory )
	{
		failed = true;
		return *this;
	}

	UniformData *header = static_cast<UniformData *>(memory);
	header->index = uniform;
	header->numElements = 1;

	int *data = reinterpret_cast<int *>(header + 1);
	*data = value;

	return *this;
}

UniformDataWriter& UniformDataWriter::SetUniformFloat( uniform_t uniform, float value )
{
	return SetUniformFloat(uniform, &value, 1);
}

UniformDataWriter& UniformDataWriter::SetUniformFloat( uniform_t uniform, float *values, size_t count )
{
	if ( shaderProgram->uniforms[uniform] == -1 )
		return *this;

	void *memory = scratch.Alloc(sizeof(UniformData) + sizeof(float)*count);
	if ( !memory )
	{
		failed = true;
		return *this;
	}

	UniformData *header = static_cast<UniformData *>(memory);
	header->index = uniform;
	header->numElements = count;
	memcpy(header + 1, values, sizeof(float) * count);

	return *this;
}

UniformDataWriter& UniformDataWriter::SetUniformVec2( uniform_t uniform, float x, float y )
{
	vec2_t values = {x, y};
	return SetUniformVec2(uniform, values);
}

UniformDataWriter& UniformDataWriter::SetUniformVec2( uniform_t uniform, const float *values, size_t count )
{
	if ( shaderProgram->uniforms[uniform] == -1 )
		return *this;

	void *memory = scratch.Alloc(sizeof(UniformData) + sizeof(vec2_t)*count);
	if ( !memory )
	{
		failed = true;
		return *this;
	}

	UniformData *header = static_cast<UniformData *>(memory);
	header->index = uniform;
	header->numElements = count;
	memcpy(header + 1, values, sizeof(vec2_t) * count);

	return *this;
}

UniformDataWriter& UniformDataWriter::SetUniformVec3( uniform_t uniform, float x, float y, float z )
{
	vec3_t values = {x, y, z};
	return SetUniformVec3(uniform, values);
}

UniformDataWriter& UniformDataWriter::SetUniformVec3( uniform_t uniform, const float *values, size_t count )
{
	if ( shaderProgram->uniforms[uniform] == -1 )
		return *this;

	void *memory = scratch.Alloc(sizeof(UniformData) + sizeof(vec3_t)*count);
	if ( !memory )
	{
		failed = true;
		return *this;
	}

	UniformData *header = static_cast<UniformData *>(memory);
	header->index = uniform;
	header->numElements = count;
	memcpy(header + 1, values, sizeof(vec3_t) * count);

	return *this;
}

UniformDataWriter& UniformDataWriter::SetUniformVec4( uniform_t uniform, float x, float y, float z, float w )
{
	vec4_t values = {x, y, z, w};
	return SetUniformVec4(uniform, values);
}

UniformDataWriter& UniformDataWriter::SetUniformVec4( uniform_t uniform, const float *values, size_t count )
{
	if ( shaderProgram->uniforms[uniform] == -1 )
		return *this;

	void *memory = scratch.Alloc(sizeof(UniformData) + sizeof(vec4_t)*count);
	if ( !memory )
	{
		failed = true;
		return *this;
	}

	UniformData *header = static_cast<UniformData *>(memory);
	header->index = uniform;
	header->numElements = count;
	memcpy(header + 1, values, sizeof(vec4_t) * count);

	return *this;
}

UniformDataWriter& UniformDataWriter::SetUniformMatrix4x3( uniform_t uniform, const float *matrix, size_t count )
{
	if ( shaderProgram->uniforms[uniform] == -1 )
		return *this;

	void *memory = scratch.Alloc(sizeof(UniformData) + sizeof(float)*12*count);
	if ( !memory )
	{
		failed = true;
		return *this;
	}

	UniformData *header = static_cast<UniformData *>(memory);
	header->index = uniform;
	header->numElements = count;
	memcpy(header + 1, matrix, sizeof(float) * 12 * count);

	return *this;
}

UniformDataWriter& UniformDataWriter::SetUniformMatrix4x4( uniform_t uniform, const float *matrix, size_t count )
{
	if ( shaderProgram->uniforms[uniform] == -1 )
		return *this;

	void *memory = scratch.Alloc(sizeof(UniformData) + sizeof(float)*16*count);
	if ( !memory )
	{
		failed = true;
		return *this;
	}

	UniformData *header = static_cast<UniformData *>(memory);
	header->index = uniform;
	header->numElements = count;
	memcpy(header + 1, matrix, sizeof(float) * 16 * count);

	return *this;
}

UniformData *UniformDataWriter::Finish( Allocator& destHeap )
{
	UniformData *endSentinel = ojkAlloc<UniformData>(scratch);
	if ( failed || !endSentinel )
	{
		return nullptr;
	}

	endSentinel->index = UNIFORM_COUNT;

	int uniformDataSize = (char *)scratch.Mark() - (char *)scratch.Base();

	// Copy scratch buffer to per-frame heap
	void *finalMemory = destHeap.Alloc(uniformDataSize);
	UniformData *result = static_cast<UniformData *>(finalMemory);
	memcpy(finalMemory, scratch.Base(), uniformDataSize);
	scratch.Reset();

	failed = false;
	shaderProgram = nullptr;

	return result;
}

SamplerBindingsWriter::SamplerBindingsWriter()
	: failed(false)
	, count(0)
{
}

SamplerBindingsWriter& SamplerBindingsWriter::AddStaticImage( image_t *image, int unit )
{
	SamplerBinding *binding = &scratch[count];
	if ( !binding )
	{
		failed = true;
		return *this;
	}

	binding->image = image;
	binding->slot = unit;
	binding->videoMapHandle = NULL_HANDLE;
	++count;

	return *this;
}

SamplerBindingsWriter& SamplerBindingsWriter::AddAnimatedImage( textureBundle_t *bundle, int unit )
{
	int index;

	if ( bundle->isVideoMap )
	{
		SamplerBinding *binding = &scratch[count];
		if ( !binding )
		{
			failed = true;
			return *this;
		}

		binding->image = nullptr;
		binding->slot = unit;
		binding->videoMapHandle = bundle->videoMapHandle + 1;
		++count;

		return *this;
	}

	if ( bundle->numImageAnimations <= 1 )
	{
		return AddStaticImage(bundle->image[0], unit);
	}

	if (backEnd.currentEntity->e.renderfx & RF_SETANIMINDEX )
	{
		index = backEnd.currentEntity->e.skinNum;
	}
	else
	{
		// it is necessary to do this messy calc to make sure animations line up
		// exactly with waveforms of the same frequency
		index = Q_ftol( tess.shaderTime * bundle->imageAnimationSpeed * FUNCTABLE_SIZE );
		index = Q_max(0, index >> FUNCTABLE_SIZE2);
	}

	if ( bundle->oneShotAnimMap )
	{
		index = Q_min(index, bundle->numImageAnimations - 1);
	}
	else
	{
		// loop
		index %= bundle->numImageAnimations;
	}

	return AddStaticImage(bundle->image[ index ], unit);
}

SamplerBinding *SamplerBindingsWriter::Finish( Allocator& destHeap, int* numBindings )
{
	if ( failed )
	{
		return nullptr;
	}

	SamplerBinding *result = ojkAllocArray<SamplerBinding>(destHeap, count);

	if ( numBindings )
	{
		*numBindings = count;
	}

	memcpy(result, scratch, sizeof(SamplerBinding)*count);
	failed = false;
	count = 0;
	return result;
}

struct Pass
{
	int maxDrawItems;
	int numDrawItems;
	DrawItem *drawItems;
	uint32_t *sortKeys;
};

static void RB_BindTextures( size_t numBindings, const SamplerBinding *bindings )
{
	for ( size_t i = 0; i < numBindings; ++i )
	{
		const SamplerBinding& binding = bindings[i];
		if ( binding.videoMapHandle )
		{
			int oldtmu = glState.currenttmu;
			GL_SelectTexture(binding.slot);
			ri.CIN_RunCinematic(binding.videoMapHandle - 1);
			ri.CIN_UploadCinematic(binding.videoMapHandle - 1);
			GL_SelectTexture(oldtmu);
		}
		else
		{
			GL_BindToTMU(binding.image, binding.slot);
		}
	}
}

static void RB_BindAndUpdateUniformBlocks( size_t numBindings, const UniformBlockBinding *bindings )
{
	for (size_t i = 0; i < numBindings; ++i)
	{
		const UniformBlockBinding& binding = bindings[i];
		if (binding.data)
			RB_BindAndUpdateUniformBlock(binding.block, binding.data);
		else
			RB_BindUniformBlock(binding.block);
	}
}

static void RB_SetRenderState(const RenderState& renderState)
{
	GL_Cull(renderState.cullType);
	GL_State(renderState.stateBits);
	GL_DepthRange(
		renderState.depthRange.minDepth,
		renderState.depthRange.maxDepth);
	if (renderState.transformFeedback)
	{
		qglEnable(GL_RASTERIZER_DISCARD);
		qglBeginTransformFeedback(GL_POINTS);
	}
}

static void RB_BindTransformFeedbackBuffer(const bufferBinding_t& binding)
{
	if (memcmp(&glState.currentXFBBO, &binding, sizeof(binding)) != 0)
	{
		if (binding.vbo != nullptr)
			qglBindBufferRange(
				GL_TRANSFORM_FEEDBACK_BUFFER,
				0,
				binding.vbo->vertexesVBO,
				binding.offset,
				binding.size);
		else
			qglBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);

		glState.currentXFBBO = binding;
	}
}

static void RB_DrawItems( int numDrawItems, const DrawItem *drawItems, uint32_t *drawOrder )
{
	for ( int i = 0; i < numDrawItems; ++i )
	{
		const DrawItem& drawItem = drawItems[drawOrder[i]];

		if (drawItem.ibo != nullptr)
			R_BindIBO(drawItem.ibo);

		GLSL_BindProgram(drawItem.program);

		GL_VertexAttribPointers(drawItem.numAttributes, drawItem.attributes);
		RB_BindTextures(drawItem.numSamplerBindings, drawItem.samplerBindings);
		RB_BindAndUpdateUniformBlocks(drawItem.numUniformBlockBindings, drawItem.uniformBlockBindings);
		RB_BindTransformFeedbackBuffer(drawItem.transformFeedbackBuffer);

		GLSL_SetUniforms(drawItem.program, drawItem.uniformData);

		RB_SetRenderState(drawItem.renderState);

		switch ( drawItem.draw.type )
		{
			case DRAW_COMMAND_MULTI_INDEXED:
			{
				GL_MultiDrawIndexed(drawItem.draw.primitiveType,
					drawItem.draw.params.multiIndexed.numIndices,
					drawItem.draw.params.multiIndexed.firstIndices,
					drawItem.draw.params.multiIndexed.numDraws);
				break;
			}

			case DRAW_COMMAND_INDEXED:
			{
				GL_DrawIndexed(drawItem.draw.primitiveType,
					drawItem.draw.params.indexed.numIndices,
					drawItem.draw.params.indexed.indexType,
					drawItem.draw.params.indexed.firstIndex,
					drawItem.draw.numInstances, 0);
				break;
			}

			case DRAW_COMMAND_ARRAYS:
			{
				GL_Draw(
					drawItem.draw.primitiveType,
					drawItem.draw.params.arrays.firstVertex,
					drawItem.draw.params.arrays.numVertices,
					drawItem.draw.numInstances);
				break;
			}

			default:
			{
				assert(!"Invalid or unhandled draw type");
				break;
			}
		}

		if (drawItem.renderState.transformFeedback)
		{
			qglEndTransformFeedback();
			qglDisable(GL_RASTERIZER_DISCARD);
		}
	}
}

void RB_AddDrawItem( Pass *pass, uint32_t sortKey, const DrawItem& drawItem )
{
	// There will be no pass if we are drawing a 2D object.
	if ( pass )
	{
		if ( pass->numDrawItems >= pass->maxDrawItems )
		{
			assert(!"Ran out of space for pass");
			return;
		}

		pass->sortKeys[pass->numDrawItems] = sortKey;
		pass->drawItems[pass->numDrawItems++] = drawItem;
	}
	else
	{
		uint32_t drawOrder[] = {0};
		RB_DrawItems(1, &drawItem, drawOrder);
	}
}

static Pass *RB_CreatePass( Allocator& allocator, int capacity )
{
	Pass *pass = ojkAlloc<Pass>(*backEndData->perFrameMemory);
	*pass = {};
	pass->maxDrawItems = capacity;
	pass->drawItems = ojkAllocArray<DrawItem>(allocator, pass->maxDrawItems);
	pass->sortKeys = ojkAllocArray<uint32_t>(allocator, pass->maxDrawItems);
	return pass;
}

void RB_StoreFrameImage()
{
	if ((backEnd.viewParms.flags & VPF_DEPTHSHADOW) ||
		(backEnd.refdef.rdflags & RDF_NOWORLDMODEL) ||
		(backEnd.refdef.rdflags & RDF_SKYBOXPORTAL) ||
		(tr.renderCubeFbo != NULL && backEnd.viewParms.targetFbo == tr.renderCubeFbo) ||
		(tr.shadowCubeFbo != NULL && backEnd.viewParms.targetFbo == tr.shadowCubeFbo) &&
		!backEnd.viewParms.isPortal)
		return;

	//store image for use in next frame, used for ssr and refraction rendering
	if (r_refraction->integer || r_ssr->integer)
	{
		FBO_t *srcFbo;

		srcFbo = glState.currentFBO;

		if (srcFbo == tr.renderCubeFbo)
			return;

		if (tr.msaaResolveFbo)
		{
			// Resolve the MSAA before anything else
			// Can't resolve just part of the MSAA FBO, so multiple views will suffer a performance hit here
			FBO_FastBlit(tr.renderFbo, NULL, tr.msaaResolveFbo, NULL, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
#if 0
			if (r_dynamicGlow->integer)
			{
				FBO_FastBlitIndexed(tr.renderFbo, tr.msaaResolveFbo, 1, 1, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
			}
#endif
			FBO_FastBlitIndexed(tr.msaaResolveFbo, tr.refractiveFbo, 0, 0, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}
		else
			FBO_FastBlitIndexed(tr.renderFbo, tr.refractiveFbo, 0, 0, GL_COLOR_BUFFER_BIT, GL_LINEAR);

		FBO_Bind(srcFbo);
	}
}

static void RB_PrepareForEntity(int entityNum, int *oldDepthRange, float originalTime)
{
	int depthRange = 0;

	if (entityNum != REFENTITYNUM_WORLD)
	{
		backEnd.currentEntity = &backEnd.refdef.entities[entityNum];
		backEnd.refdef.floatTime = originalTime - backEnd.currentEntity->e.shaderTime;
		// we have to reset the shaderTime as well otherwise image animations start
		// from the wrong frame
		tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;

		// set up the transformation matrix
		R_RotateForEntity(backEnd.currentEntity, &backEnd.viewParms, &backEnd.ori);

		if (backEnd.currentEntity->e.renderfx & RF_NODEPTH) {
			// No depth at all, very rare but some things for seeing through walls
			depthRange = 2;
		}
		else if (backEnd.currentEntity->e.renderfx & RF_DEPTHHACK) {
			// hack the depth range to prevent view model from poking into walls
			depthRange = 1;
		}
	}
	else {
		backEnd.currentEntity = &tr.worldEntity;
		backEnd.refdef.floatTime = originalTime;
		backEnd.ori = backEnd.viewParms.world;

		// we have to reset the shaderTime as well otherwise image animations on
		// the world (like water) continue with the wrong frame
		tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
	}

	GL_SetModelviewMatrix(backEnd.ori.modelViewMatrix);

	// change depthrange. Also change projection matrix so first person weapon
	// does not look like coming out of the screen.
	if (*oldDepthRange != depthRange)
	{
		switch (depthRange)
		{
		default:
		case 0:
			if (backEnd.viewParms.stereoFrame != STEREO_CENTER)
			{
				GL_SetProjectionMatrix(backEnd.viewParms.projectionMatrix);
			}
			break;

		case 1:
			if (backEnd.viewParms.stereoFrame != STEREO_CENTER)
			{
				viewParms_t temp = backEnd.viewParms;
				R_SetupProjection(&temp, r_znear->value, 0, qfalse);
				GL_SetProjectionMatrix(temp.projectionMatrix);
			}
			break;

		case 2:
			if (backEnd.viewParms.stereoFrame != STEREO_CENTER)
			{
				viewParms_t temp = backEnd.viewParms;
				R_SetupProjection(&temp, r_znear->value, 0, qfalse);
				GL_SetProjectionMatrix(temp.projectionMatrix);
			}
			break;
		}

		*oldDepthRange = depthRange;
	}
}

static void RB_SubmitDrawSurfsForDepthFill(
	drawSurf_t *drawSurfs,
	int numDrawSurfs,
	float originalTime)
{
	shader_t *oldShader = nullptr;
	int oldEntityNum = -1;
	int oldSort = -1;
	int oldDepthRange = 0;

	drawSurf_t *drawSurf = drawSurfs;
	for (int i = 0; i < numDrawSurfs; i++, drawSurf++)
	{
		shader_t *shader;
		int cubemapIndex;
		int postRender;
		int entityNum;

		R_DecomposeSort(drawSurf->sort, &entityNum, &shader, &cubemapIndex, &postRender);
		assert(shader != nullptr);

		if (shader->sort != SS_OPAQUE ||
			shader->isSky)
		{
			// Don't draw yet, let's see what's to come
			continue;
		}

		if (shader == oldShader &&	entityNum == oldEntityNum)
		{
			// fast path, same as previous sort
			rb_surfaceTable[*drawSurf->surface](drawSurf->surface);
			continue;
		}

		// change the tess parameters if needed
		// a "entityMergable" shader is a shader that can have surfaces from
		// seperate entities merged into a single batch, like smoke and blood
		// puff sprites
		if (shader != oldShader ||
			(entityNum != oldEntityNum && !shader->entityMergable))
		{
			if (oldShader != nullptr)
			{
				RB_EndSurface();
			}

			RB_BeginSurface(shader, 0, 0);
			backEnd.pc.c_surfBatches++;
			oldShader = shader;
		}

		oldSort = drawSurf->sort;

		// change the modelview matrix if needed
		if (entityNum != oldEntityNum)
		{
			RB_PrepareForEntity(entityNum, &oldDepthRange, originalTime);
			oldEntityNum = entityNum;
		}

		// add the triangles for this surface
		rb_surfaceTable[*drawSurf->surface](drawSurf->surface);
	}

	// draw the contents of the last shader batch
	if (oldShader != nullptr)
	{
		RB_EndSurface();
	}
}

static void RB_SubmitDrawSurfs(
	drawSurf_t *drawSurfs,
	int numDrawSurfs,
	float originalTime)
{
	shader_t *oldShader = nullptr;
	int oldEntityNum = -1;
	int oldSort = -1;
	int oldFogNum = -1;
	int oldDepthRange = 0;
	int oldDlighted = 0;
	int oldPostRender = 0;
	int oldCubemapIndex = -1;

	drawSurf_t *drawSurf = drawSurfs;
	for (int i = 0; i < numDrawSurfs; i++, drawSurf++)
	{
		shader_t *shader;
		int cubemapIndex;
		int postRender;
		int entityNum;
		int fogNum;
		int dlighted;

		R_DecomposeSort(drawSurf->sort, &entityNum, &shader, &cubemapIndex, &postRender);
		assert(shader != nullptr);
		fogNum = drawSurf->fogIndex;
		dlighted = drawSurf->dlightBits;

		if (shader == oldShader &&
			fogNum == oldFogNum &&
			postRender == oldPostRender &&
			cubemapIndex == oldCubemapIndex &&
			entityNum == oldEntityNum &&
			dlighted == oldDlighted)
		{
			// fast path, same as previous sort
			rb_surfaceTable[*drawSurf->surface](drawSurf->surface);
			continue;
		}

		oldSort = drawSurf->sort;

		//
		// change the tess parameters if needed
		// a "entityMergable" shader is a shader that can have surfaces from seperate
		// entities merged into a single batch, like smoke and blood puff sprites
		if ((shader != oldShader ||
			fogNum != oldFogNum ||
			dlighted != oldDlighted ||
			postRender != oldPostRender ||
			cubemapIndex != oldCubemapIndex ||
			(entityNum != oldEntityNum && !shader->entityMergable)))
		{
			if (oldShader != nullptr)
			{
				RB_EndSurface();
			}

			RB_BeginSurface(shader, fogNum, cubemapIndex);
			backEnd.pc.c_surfBatches++;
			oldShader = shader;
			oldFogNum = fogNum;
			oldDlighted = dlighted;
			oldPostRender = postRender;
			oldCubemapIndex = cubemapIndex;
		}

		if (entityNum != oldEntityNum)
		{
			RB_PrepareForEntity(entityNum, &oldDepthRange, originalTime);

			// set up the dynamic lighting if needed
			if (entityNum == REFENTITYNUM_WORLD || backEnd.currentEntity->needDlights)
			{
				R_TransformDlights(
					backEnd.refdef.num_dlights,
					backEnd.refdef.dlights,
					&backEnd.ori);
			}

			oldEntityNum = entityNum;
		}

		// add the triangles for this surface
		rb_surfaceTable[*drawSurf->surface](drawSurf->surface);
	}

	// draw the contents of the last shader batch
	if (oldShader != nullptr)
	{
		RB_EndSurface();
	}
}

static void RB_SubmitRenderPass(
	Pass& renderPass,
	Allocator& allocator)
{
	uint32_t *drawOrder = ojkAllocArray<uint32_t>(
		allocator, renderPass.numDrawItems);

	uint32_t numDrawItems = renderPass.numDrawItems;
	for (uint32_t i = 0; i < numDrawItems; ++i)
		drawOrder[i] = i;

	uint32_t *sortKeys = renderPass.sortKeys;
	std::sort(drawOrder, drawOrder + numDrawItems, [sortKeys](uint32_t a, uint32_t b)
	{
		return sortKeys[a] < sortKeys[b];
	});

	RB_DrawItems(renderPass.numDrawItems, renderPass.drawItems, drawOrder);
}

/*
==================
RB_RenderDrawSurfList
==================
*/
static void RB_RenderDrawSurfList(drawSurf_t *drawSurfs, int numDrawSurfs)
{
	/*
	merging surfaces together that share the same shader (e.g. polys, patches)
	upload per frame data - but this might be the same between render passes?

	how about:
	tr.refdef.entities[]

	and .... entityCullInfo_t tr.refdef.entityCullInfo[]
	struct visibleEntity_t
	{
	uint32_t frustumMask; // bitfield of frustums which intersect
	EntityId entityId;
	};

	foreach ghoul2 model:
	transform bones

	foreach visibleEntity:
	upload per frame data

	for polygons:
	merge them, create new surface and upload data

	for patch meshes:
	merge them, create new surface and upload data


	each surface corresponds to something which has all of its gpu data uploaded
	*/

	// Prepare memory for the current render pass
	void *allocMark = backEndData->perFrameMemory->Mark();
	assert(backEndData->currentPass == nullptr);
	backEndData->currentPass = RB_CreatePass(
		*backEndData->perFrameMemory, numDrawSurfs * 5);

	backEndData->currentPostPass = RB_CreatePass(
		*backEndData->perFrameMemory, numDrawSurfs);

	// save original time for entity shader offsets
	float originalTime = backEnd.refdef.floatTime;
	FBO_t *fbo = glState.currentFBO;

	backEnd.currentEntity = &tr.worldEntity;
	backEnd.pc.c_surfaces += numDrawSurfs;

	if (backEnd.renderPass != MAIN_PASS)
	{
		if (backEnd.renderPass == PRE_PASS)
			FBO_Bind(tr.preBuffersFbo);

		RB_SubmitDrawSurfsForDepthFill(drawSurfs, numDrawSurfs, originalTime);
	}
	else
	{
		RB_SubmitDrawSurfs(drawSurfs, numDrawSurfs, originalTime);
	}

	// Do the drawing and release memory
	RB_SubmitRenderPass(
		*backEndData->currentPass,
		*backEndData->perFrameMemory);
	
	if (backEnd.renderPass == MAIN_PASS)
	{
		RB_StoreFrameImage();
		RB_SubmitRenderPass(
			*backEndData->currentPostPass,
			*backEndData->perFrameMemory);
	}

	backEndData->perFrameMemory->ResetTo(allocMark);
	backEndData->currentPass = nullptr;

	// Reset things to how they were
	backEnd.refdef.floatTime = originalTime;
	FBO_Bind(fbo);
	GL_SetModelviewMatrix(backEnd.viewParms.world.modelViewMatrix);
}


/*
============================================================================

RENDER BACK END FUNCTIONS

============================================================================
*/

/*
================
RB_SetGL2D

================
*/
void	RB_SetGL2D (void) {
	matrix_t matrix;
	int width, height;

	if (backEnd.projection2D && backEnd.last2DFBO == glState.currentFBO)
		return;

	backEnd.projection2D = qtrue;
	backEnd.last2DFBO = glState.currentFBO;

	if (glState.currentFBO)
	{
		width = glState.currentFBO->width;
		height = glState.currentFBO->height;
	}
	else
	{
		width = glConfig.vidWidth;
		height = glConfig.vidHeight;
	}

	// set 2D virtual screen size
	qglViewport( 0, 0, width, height );
	qglScissor( 0, 0, width, height );

	Matrix16Ortho(0, 640, 480, 0, 0, 1, matrix);
	GL_SetProjectionMatrix(matrix);
	Matrix16Identity(matrix);
	GL_SetModelviewMatrix(matrix);

	GL_State( GLS_DEPTHTEST_DISABLE |
			  GLS_SRCBLEND_SRC_ALPHA |
			  GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );

	GL_Cull(CT_TWO_SIDED);

	// set time for 2D shaders
	backEnd.refdef.time = ri.Milliseconds();
	backEnd.refdef.floatTime = backEnd.refdef.time * 0.001f;

	// reset color scaling
	backEnd.refdef.colorScale = 1.0f;
}


/*
=============
RE_StretchRaw

FIXME: not exactly backend
Stretches a raw 32 bit power of 2 bitmap image over the given screen rectangle.
Used for cinematics.
=============
*/
void RE_StretchRaw (int x, int y, int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty) {
	int			i, j;
	int			start, end;
	vec4_t quadVerts[4];
	vec2_t texCoords[4];

	if ( !tr.registered ) {
		return;
	}
	R_IssuePendingRenderCommands();

	if ( tess.numIndexes ) {
		RB_EndSurface();
	}

	// we definately want to sync every frame for the cinematics
	qglFinish();

	start = 0;
	if ( r_speeds->integer ) {
		start = ri.Milliseconds();
	}

	// make sure rows and cols are powers of 2
	for ( i = 0 ; ( 1 << i ) < cols ; i++ ) {
	}
	for ( j = 0 ; ( 1 << j ) < rows ; j++ ) {
	}
	if ( ( 1 << i ) != cols || ( 1 << j ) != rows) {
		ri.Error (ERR_DROP, "Draw_StretchRaw: size not a power of 2: %i by %i", cols, rows);
	}

	RE_UploadCinematic (cols, rows, data, client, dirty);

	if ( r_speeds->integer ) {
		end = ri.Milliseconds();
		ri.Printf( PRINT_ALL, "qglTexSubImage2D %i, %i: %i msec\n", cols, rows, end - start );
	}

	// FIXME: HUGE hack
	if (!tr.renderFbo || backEnd.framePostProcessed)
	{
		FBO_Bind(NULL);
	}
	else
	{
		FBO_Bind(tr.renderFbo);
	}

	RB_SetGL2D();

	VectorSet4(quadVerts[0], x,     y,     0.0f, 1.0f);
	VectorSet4(quadVerts[1], x + w, y,     0.0f, 1.0f);
	VectorSet4(quadVerts[2], x + w, y + h, 0.0f, 1.0f);
	VectorSet4(quadVerts[3], x,     y + h, 0.0f, 1.0f);

	VectorSet2(texCoords[0], 0.5f / cols,          0.5f / rows);
	VectorSet2(texCoords[1], (cols - 0.5f) / cols, 0.5f / rows);
	VectorSet2(texCoords[2], (cols - 0.5f) / cols, (rows - 0.5f) / rows);
	VectorSet2(texCoords[3], 0.5f / cols,          (rows - 0.5f) / rows);

	GLSL_BindProgram(&tr.textureColorShader);
	
	GLSL_SetUniformMatrix4x4(&tr.textureColorShader, UNIFORM_MODELVIEWPROJECTIONMATRIX, glState.modelviewProjection);
	GLSL_SetUniformVec4(&tr.textureColorShader, UNIFORM_COLOR, colorWhite);

	RB_InstantQuad2(quadVerts, texCoords);
}

void RE_UploadCinematic (int cols, int rows, const byte *data, int client, qboolean dirty) {

	GL_Bind( tr.scratchImage[client] );

	// if the scratchImage isn't in the format we want, specify it as a new texture
	if ( cols != tr.scratchImage[client]->width || rows != tr.scratchImage[client]->height ) {
		tr.scratchImage[client]->width = tr.scratchImage[client]->uploadWidth = cols;
		tr.scratchImage[client]->height = tr.scratchImage[client]->uploadHeight = rows;
		qglTexImage2D( GL_TEXTURE_2D, 0, GL_RGB8, cols, rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, data );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );	
	} else {
		if (dirty) {
			// otherwise, just subimage upload it so that drivers can tell we are going to be changing
			// it and don't try and do a texture compression
			qglTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, cols, rows, GL_RGBA, GL_UNSIGNED_BYTE, data );
		}
	}
}


/*
=============
RB_SetColor

=============
*/
static const void	*RB_SetColor( const void *data ) {
	const setColorCommand_t	*cmd;

	cmd = (const setColorCommand_t *)data;

	backEnd.color2D[0] = cmd->color[0];
	backEnd.color2D[1] = cmd->color[1];
	backEnd.color2D[2] = cmd->color[2];
	backEnd.color2D[3] = cmd->color[3];

	return (const void *)(cmd + 1);
}

/*
=============
RB_StretchPic
=============
*/
static const void *RB_StretchPic ( const void *data ) {
	const stretchPicCommand_t	*cmd;
	shader_t *shader;

	cmd = (const stretchPicCommand_t *)data;

	// FIXME: HUGE hack
	if (!tr.renderFbo || backEnd.framePostProcessed)
	{
		FBO_Bind(NULL);
	}
	else
	{
		FBO_Bind(tr.renderFbo);
	}

	RB_SetGL2D();

	shader = cmd->shader;
	if ( shader != tess.shader ) {
		if ( tess.numIndexes ) {
			RB_EndSurface();
		}
		backEnd.currentEntity = &backEnd.entity2D;
		RB_BeginSurface( shader, 0, 0 );
	}

	RB_CHECKOVERFLOW( 4, 6 );
	int numVerts = tess.numVertexes;
	int numIndexes = tess.numIndexes;

	tess.numVertexes += 4;
	tess.numIndexes += 6;

	tess.indexes[ numIndexes ] = numVerts + 3;
	tess.indexes[ numIndexes + 1 ] = numVerts + 0;
	tess.indexes[ numIndexes + 2 ] = numVerts + 2;
	tess.indexes[ numIndexes + 3 ] = numVerts + 2;
	tess.indexes[ numIndexes + 4 ] = numVerts + 0;
	tess.indexes[ numIndexes + 5 ] = numVerts + 1;

	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts ]);
	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts + 1 ]);
	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts + 2 ]);
	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts + 3 ]);

	tess.xyz[ numVerts ][0] = cmd->x;
	tess.xyz[ numVerts ][1] = cmd->y;
	tess.xyz[ numVerts ][2] = 0;

	tess.texCoords[ numVerts ][0][0] = cmd->s1;
	tess.texCoords[ numVerts ][0][1] = cmd->t1;

	tess.xyz[ numVerts + 1 ][0] = cmd->x + cmd->w;
	tess.xyz[ numVerts + 1 ][1] = cmd->y;
	tess.xyz[ numVerts + 1 ][2] = 0;

	tess.texCoords[ numVerts + 1 ][0][0] = cmd->s2;
	tess.texCoords[ numVerts + 1 ][0][1] = cmd->t1;

	tess.xyz[ numVerts + 2 ][0] = cmd->x + cmd->w;
	tess.xyz[ numVerts + 2 ][1] = cmd->y + cmd->h;
	tess.xyz[ numVerts + 2 ][2] = 0;

	tess.texCoords[ numVerts + 2 ][0][0] = cmd->s2;
	tess.texCoords[ numVerts + 2 ][0][1] = cmd->t2;

	tess.xyz[ numVerts + 3 ][0] = cmd->x;
	tess.xyz[ numVerts + 3 ][1] = cmd->y + cmd->h;
	tess.xyz[ numVerts + 3 ][2] = 0;

	tess.texCoords[ numVerts + 3 ][0][0] = cmd->s1;
	tess.texCoords[ numVerts + 3 ][0][1] = cmd->t2;

	return (const void *)(cmd + 1);
}

/*
=============
RB_DrawRotatePic
=============
*/
static const void *RB_RotatePic ( const void *data ) 
{
	const rotatePicCommand_t	*cmd;
	shader_t *shader;

	cmd = (const rotatePicCommand_t *)data;

	// FIXME: HUGE hack
	if (!tr.renderFbo || backEnd.framePostProcessed)
	{
		FBO_Bind(NULL);
	}
	else
	{
		FBO_Bind(tr.renderFbo);
	}

	RB_SetGL2D();

	shader = cmd->shader;
	if ( shader != tess.shader ) {
		if ( tess.numIndexes ) {
			RB_EndSurface();
		}
		backEnd.currentEntity = &backEnd.entity2D;
		RB_BeginSurface( shader, 0, 0 );
	}

	RB_CHECKOVERFLOW( 4, 6 );
	int numVerts = tess.numVertexes;
	int numIndexes = tess.numIndexes;

	float angle = DEG2RAD( cmd->a );
	float s = sinf( angle );
	float c = cosf( angle );

	matrix3_t m = {
		{ c, s, 0.0f },
		{ -s, c, 0.0f },
		{ cmd->x + cmd->w, cmd->y, 1.0f }
	};

	tess.numVertexes += 4;
	tess.numIndexes += 6;

	tess.indexes[ numIndexes ] = numVerts + 3;
	tess.indexes[ numIndexes + 1 ] = numVerts + 0;
	tess.indexes[ numIndexes + 2 ] = numVerts + 2;
	tess.indexes[ numIndexes + 3 ] = numVerts + 2;
	tess.indexes[ numIndexes + 4 ] = numVerts + 0;
	tess.indexes[ numIndexes + 5 ] = numVerts + 1;

	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts ]);
	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts + 1]);
	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts + 2]);
	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts + 3 ]);

	tess.xyz[ numVerts ][0] = m[0][0] * (-cmd->w) + m[2][0];
	tess.xyz[ numVerts ][1] = m[0][1] * (-cmd->w) + m[2][1];
	tess.xyz[ numVerts ][2] = 0;

	tess.texCoords[ numVerts ][0][0] = cmd->s1;
	tess.texCoords[ numVerts ][0][1] = cmd->t1;

	tess.xyz[ numVerts + 1 ][0] = m[2][0];
	tess.xyz[ numVerts + 1 ][1] = m[2][1];
	tess.xyz[ numVerts + 1 ][2] = 0;

	tess.texCoords[ numVerts + 1 ][0][0] = cmd->s2;
	tess.texCoords[ numVerts + 1 ][0][1] = cmd->t1;

	tess.xyz[ numVerts + 2 ][0] = m[1][0] * (cmd->h) + m[2][0];
	tess.xyz[ numVerts + 2 ][1] = m[1][1] * (cmd->h) + m[2][1];
	tess.xyz[ numVerts + 2 ][2] = 0;

	tess.texCoords[ numVerts + 2 ][0][0] = cmd->s2;
	tess.texCoords[ numVerts + 2 ][0][1] = cmd->t2;

	tess.xyz[ numVerts + 3 ][0] = m[0][0] * (-cmd->w) + m[1][0] * (cmd->h) + m[2][0];
	tess.xyz[ numVerts + 3 ][1] = m[0][1] * (-cmd->w) + m[1][1] * (cmd->h) + m[2][1];
	tess.xyz[ numVerts + 3 ][2] = 0;

	tess.texCoords[ numVerts + 3 ][0][0] = cmd->s1;
	tess.texCoords[ numVerts + 3 ][0][1] = cmd->t2;

	return (const void *)(cmd + 1);
}

/*
=============
RB_RotatePic2RatioFix
=============
*/
static float ratio = 1.0f;
static const void *RB_RotatePic2RatioFix ( const void *data )
{
	const rotatePicRatioFixCommand_t *cmd;

	cmd = (const rotatePicRatioFixCommand_t *)data;

	if (cmd->ratio <= 0.0f)
		ratio = 1.0f;
	else
		ratio = cmd->ratio;
	return (const void *)(cmd + 1);
}

/*
=============
RB_DrawRotatePic2
=============
*/
static const void *RB_RotatePic2 ( const void *data ) 
{
	const rotatePicCommand_t	*cmd;
	shader_t *shader;

	cmd = (const rotatePicCommand_t *)data;

	// FIXME: HUGE hack
	if (!tr.renderFbo || backEnd.framePostProcessed)
	{
		FBO_Bind(NULL);
	}
	else
	{
		FBO_Bind(tr.renderFbo);
	}

	RB_SetGL2D();

	shader = cmd->shader;
	if ( shader != tess.shader ) {
		if ( tess.numIndexes ) {
			RB_EndSurface();
		}
		backEnd.currentEntity = &backEnd.entity2D;
		RB_BeginSurface( shader, 0, 0 );
	}

	RB_CHECKOVERFLOW( 4, 6 );
	int numVerts = tess.numVertexes;
	int numIndexes = tess.numIndexes;

	float angle = DEG2RAD( cmd->a );
	float s = sinf( angle );
	float c = cosf( angle );

	matrix3_t m = {
		{ c*ratio, s, 0.0f },
		{ -s*ratio, c, 0.0f },
		{ cmd->x, cmd->y, 1.0f }
	};

	tess.numVertexes += 4;
	tess.numIndexes += 6;

	tess.indexes[ numIndexes ] = numVerts + 3;
	tess.indexes[ numIndexes + 1 ] = numVerts + 0;
	tess.indexes[ numIndexes + 2 ] = numVerts + 2;
	tess.indexes[ numIndexes + 3 ] = numVerts + 2;
	tess.indexes[ numIndexes + 4 ] = numVerts + 0;
	tess.indexes[ numIndexes + 5 ] = numVerts + 1;

	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts ]);
	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts + 1]);
	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts + 2]);
	VectorCopy4(backEnd.color2D, tess.vertexColors[ numVerts + 3 ]);

	tess.xyz[ numVerts ][0] = m[0][0] * (-cmd->w * 0.5f) + m[1][0] * (-cmd->h * 0.5f) + m[2][0];
	tess.xyz[ numVerts ][1] = m[0][1] * (-cmd->w * 0.5f) + m[1][1] * (-cmd->h * 0.5f) + m[2][1];
	tess.xyz[ numVerts ][2] = 0;

	tess.texCoords[ numVerts ][0][0] = cmd->s1;
	tess.texCoords[ numVerts ][0][1] = cmd->t1;

	tess.xyz[ numVerts + 1 ][0] = m[0][0] * (cmd->w * 0.5f) + m[1][0] * (-cmd->h * 0.5f) + m[2][0];
	tess.xyz[ numVerts + 1 ][1] = m[0][1] * (cmd->w * 0.5f) + m[1][1] * (-cmd->h * 0.5f) + m[2][1];
	tess.xyz[ numVerts + 1 ][2] = 0;

	tess.texCoords[ numVerts + 1 ][0][0] = cmd->s2;
	tess.texCoords[ numVerts + 1 ][0][1] = cmd->t1;

	tess.xyz[ numVerts + 2 ][0] = m[0][0] * (cmd->w * 0.5f) + m[1][0] * (cmd->h * 0.5f) + m[2][0];
	tess.xyz[ numVerts + 2 ][1] = m[0][1] * (cmd->w * 0.5f) + m[1][1] * (cmd->h * 0.5f) + m[2][1];
	tess.xyz[ numVerts + 2 ][2] = 0;

	tess.texCoords[ numVerts + 2 ][0][0] = cmd->s2;
	tess.texCoords[ numVerts + 2 ][0][1] = cmd->t2;

	tess.xyz[ numVerts + 3 ][0] = m[0][0] * (-cmd->w * 0.5f) + m[1][0] * (cmd->h * 0.5f) + m[2][0];
	tess.xyz[ numVerts + 3 ][1] = m[0][1] * (-cmd->w * 0.5f) + m[1][1] * (cmd->h * 0.5f) + m[2][1];
	tess.xyz[ numVerts + 3 ][2] = 0;

	tess.texCoords[ numVerts + 3 ][0][0] = cmd->s1;
	tess.texCoords[ numVerts + 3 ][0][1] = cmd->t2;

	return (const void *)(cmd + 1);
}

/*
=============
RB_ScissorPic
=============
*/
const void *RB_Scissor(const void *data)
{
	const scissorCommand_t	*cmd;

	cmd = (const scissorCommand_t *)data;

	if (!backEnd.projection2D)
	{
		RB_SetGL2D();
	}

	if (cmd->x >= 0)
	{
		qglScissor(cmd->x, (glConfig.vidHeight - cmd->y - cmd->h), cmd->w, cmd->h);
	}
	else
	{
		qglScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	}

	return (const void *)(cmd + 1);
}

/*
=============
RB_ProjectCubeMap
=============
*/
static const void *RB_ProjectCubeMap(const void *data) {

	const projectCubemapCommand_t *cmd = (const projectCubemapCommand_t *)data;

	// finish any 2D drawing if needed
	if (tess.numIndexes)
		RB_EndSurface();

	RB_SetGL2D();

	cubemap_t *cubemap = cmd->cubemap;

	int width = cubemap->image->width;
	int height = cubemap->image->height;
	
	FBO_Bind(tr.renderEquirectangularFbo);
	qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, cubemap->image->texnum, 0);
	qglViewport(0, 0, width, height);
	qglScissor(0, 0, width, height);
	GL_BindToTMU(tr.renderCubeImage, TB_CUBEMAP);
	GLSL_BindProgram(&tr.equirectangularShader);
	qglDrawArrays(GL_TRIANGLES, 0, 3);

	qglViewport(0, 0, r_cubemapSize->integer, r_cubemapSize->integer);
	qglScissor(0, 0, r_cubemapSize->integer, r_cubemapSize->integer);

	return (const void *)(cmd + 1);
}

/*
=============
RB_PrefilterEnvMap
=============
*/
static const void *RB_PrefilterEnvMap(const void *data) {

	const convolveCubemapCommand_t *cmd = (const convolveCubemapCommand_t *)data;

	// finish any 2D drawing if needed
	if (tess.numIndexes)
		RB_EndSurface();

	RB_SetGL2D();

	cubemap_t *cubemap = cmd->cubemap;

	if (!cubemap || !cmd)
		return (const void *)(cmd + 1);

	int cubeMipSize = r_cubemapSize->integer;
	int numMips = 0;
	float numRoughnessMips = 0.0f;
	
	int width = cubemap->image->width;
	int height = cubemap->image->height;

	while (cubeMipSize)
	{
		cubeMipSize >>= 1;
		numMips++;
	}
	numRoughnessMips = MAX(1.0f, numMips - 4.0f);

	FBO_Bind(tr.preFilterEnvMapFbo);

	if (r_cubeMapping->integer > 1)
		GL_BindToTMU(tr.renderCubeImage, TB_CUBEMAP);
	else
		GL_BindToTMU(cubemap->image, TB_CUBEMAP);
	
	GLSL_BindProgram(&tr.prefilterEnvMapShader);

	for (int level = 1; level <= numMips - 4; level++)
	{
		width = width / 2;
		height = height / 2;
		qglViewport(0, 0, width, height);
		qglScissor(0, 0, width, height);

		vec4_t viewInfo;
		VectorSet4(viewInfo, cmd->cubeSide, level, numRoughnessMips, level / numRoughnessMips);
		GLSL_SetUniformVec4(&tr.prefilterEnvMapShader, UNIFORM_VIEWINFO, viewInfo); 

		qglTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, level- 1);

		qglDrawArrays(GL_TRIANGLES, 0, 3);

		if (r_cubeMapping->integer > 1)
			qglCopyTexSubImage2D(GL_COLOR_ATTACHMENT0, level, 0, 0, 0, 0, width, height);
		else
			qglCopyTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + cmd->cubeSide, level, 0, 0, 0, 0, width, height);
	}

	return (const void *)(cmd + 1);
}

static void RB_BuildHiZBuffer()
{
	if ((backEnd.viewParms.flags & VPF_DEPTHSHADOW) ||
		(backEnd.refdef.rdflags & RDF_NOWORLDMODEL) ||
		(backEnd.refdef.rdflags & RDF_SKYBOXPORTAL) ||
		(tr.renderCubeFbo != NULL && backEnd.viewParms.targetFbo == tr.renderCubeFbo) ||
		(tr.shadowCubeFbo != NULL && backEnd.viewParms.targetFbo == tr.shadowCubeFbo))
		return;

	if (r_ssr->integer)
	{
		FBO_t *srcFbo;

		srcFbo = glState.currentFBO;

		int width = tr.renderDepthImage->width;
		int height = tr.renderDepthImage->height;

		GL_State(GLS_DEPTHTEST_DISABLE);
		GL_Cull(CT_FRONT_SIDED);

		//FIX ME: maybe store mips count as property of the image?
		int dim = width > height ? width : height;
		int levels = 0;
		while (dim) {
			levels++;
			dim /= 2;
		}

		for (int i = 0; i < 1; i++)
		{
			FBO_Bind(tr.prevDepthFbo);

			qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tr.prevRenderDepthImage->texnum, i);

			width = width / 2.0;
			height = height / 2.0;
			qglViewport(0, 0, width, height);
			qglScissor(0, 0, width, height);

			GLSL_BindProgram(&tr.hiZDownsample);

			if (i == 0)
				GL_BindToTMU(tr.renderDepthImage, TB_DIFFUSEMAP);
			else
				GL_BindToTMU(tr.prevRenderDepthImage, TB_DIFFUSEMAP);

			float mip = MAX(0, i - 1);

			matrix_t invProjectionMatrix;
			Matrix16Inverse(backEnd.viewParms.projectionMatrix, invProjectionMatrix);
			GLSL_SetUniformMatrix4x4(&tr.hiZDownsample, UNIFORM_INVVIEWPROJECTIONMATRIX, invProjectionMatrix);

			vec4_t viewInfo;
			VectorSet4(viewInfo, (float)mip, (float)(i + 1), tr.viewParms.zNear, tr.viewParms.zFar);
			GLSL_SetUniformVec4(&tr.hiZDownsample, UNIFORM_VIEWINFO, viewInfo);
			qglDrawArrays(GL_TRIANGLES, 0, 3);
		}

		qglViewport(0, 0, tr.renderDepthImage->width, tr.renderDepthImage->height);
		qglScissor(0, 0, tr.renderDepthImage->width, tr.renderDepthImage->height);
		FBO_Bind(srcFbo);
	}
}

static void RB_RenderSunShadows()
{
	if ((backEnd.viewParms.flags & VPF_DEPTHSHADOW) ||
		(backEnd.refdef.rdflags & RDF_NOWORLDMODEL) ||
		(tr.shadowCubeFbo != NULL && backEnd.viewParms.targetFbo == tr.shadowCubeFbo))
		return;

	FBO_t *shadowFbo = tr.screenShadowFbo;

	vec4_t quadVerts[4];
	vec2_t texCoords[4];
	vec4_t box;

	FBO_Bind(shadowFbo);

	const float viewportScaleX = shadowFbo->width / glConfig.vidWidth;
	const float viewportScaleY = shadowFbo->height / glConfig.vidHeight;

	box[0] = backEnd.viewParms.viewportX * viewportScaleX;
	box[1] = backEnd.viewParms.viewportY * viewportScaleY;
	box[2] = backEnd.viewParms.viewportWidth  * viewportScaleX;
	box[3] = backEnd.viewParms.viewportHeight * viewportScaleY;

	qglViewport(box[0], box[1], box[2], box[3]);
	qglScissor(box[0], box[1], box[2], box[3]);

	box[0] = backEnd.viewParms.viewportX / (float)glConfig.vidWidth;
	box[1] = backEnd.viewParms.viewportY / (float)glConfig.vidHeight;
	box[2] = box[0] + backEnd.viewParms.viewportWidth / (float)glConfig.vidWidth;
	box[3] = box[1] + backEnd.viewParms.viewportHeight / (float)glConfig.vidHeight;

	texCoords[0][0] = box[0]; texCoords[0][1] = box[3];
	texCoords[1][0] = box[2]; texCoords[1][1] = box[3];
	texCoords[2][0] = box[2]; texCoords[2][1] = box[1];
	texCoords[3][0] = box[0]; texCoords[3][1] = box[1];

	box[0] = -1.0f;
	box[1] = -1.0f;
	box[2] = 1.0f;
	box[3] = 1.0f;

	VectorSet4(quadVerts[0], box[0], box[3], 0, 1);
	VectorSet4(quadVerts[1], box[2], box[3], 0, 1);
	VectorSet4(quadVerts[2], box[2], box[1], 0, 1);
	VectorSet4(quadVerts[3], box[0], box[1], 0, 1);

	GL_State(GLS_DEPTHTEST_DISABLE);
	GLSL_BindProgram(&tr.shadowmaskShader);

	GL_BindToTMU(tr.renderDepthImage, TB_COLORMAP);
	GL_BindToTMU(tr.sunShadowDepthImage[0], TB_SHADOWMAP);
	GL_BindToTMU(tr.sunShadowDepthImage[1], TB_SHADOWMAP2);
	GL_BindToTMU(tr.sunShadowDepthImage[2], TB_SHADOWMAP3); 
	GL_BindToTMU(tr.sunShadowDepthImage[3], TB_SHADOWMAP4);

	GLSL_SetUniformMatrix4x4(
		&tr.shadowmaskShader,
		UNIFORM_SHADOWMVP,
		backEnd.refdef.sunShadowMvp[0]);
	GLSL_SetUniformMatrix4x4(
		&tr.shadowmaskShader,
		UNIFORM_SHADOWMVP2,
		backEnd.refdef.sunShadowMvp[1]);
	GLSL_SetUniformMatrix4x4(
		&tr.shadowmaskShader,
		UNIFORM_SHADOWMVP3,
		backEnd.refdef.sunShadowMvp[2]);
	GLSL_SetUniformVec3(
		&tr.shadowmaskShader,
		UNIFORM_VIEWORIGIN,
		backEnd.refdef.vieworg);

	const float zmax = backEnd.viewParms.zFar;
	const float ymax = zmax * tanf(backEnd.viewParms.fovY * M_PI / 360.0f);
	const float xmax = zmax * tanf(backEnd.viewParms.fovX * M_PI / 360.0f);

	const float zmin = r_znear->value;

	vec3_t viewBasis[3];
	VectorScale(backEnd.refdef.viewaxis[0], zmax, viewBasis[0]);
	VectorScale(backEnd.refdef.viewaxis[1], xmax, viewBasis[1]);
	VectorScale(backEnd.refdef.viewaxis[2], ymax, viewBasis[2]);

	GLSL_SetUniformVec3(&tr.shadowmaskShader, UNIFORM_VIEWFORWARD, viewBasis[0]);
	GLSL_SetUniformVec3(&tr.shadowmaskShader, UNIFORM_VIEWLEFT, viewBasis[1]);
	GLSL_SetUniformVec3(&tr.shadowmaskShader, UNIFORM_VIEWUP, viewBasis[2]);

	const vec4_t viewInfo = { zmax / zmin, zmax, 0.0f, 0.0f };
	GLSL_SetUniformVec4(&tr.shadowmaskShader, UNIFORM_VIEWINFO, viewInfo);

	RB_InstantQuad2(quadVerts, texCoords);
}

static void RB_RenderSSAO()
{
	if ((backEnd.viewParms.flags & VPF_DEPTHSHADOW) ||
		(backEnd.refdef.rdflags & RDF_NOWORLDMODEL) ||
		(tr.shadowCubeFbo != NULL && backEnd.viewParms.targetFbo == tr.shadowCubeFbo))
		return;

	const float zmax = backEnd.viewParms.zFar;
	const float zmin = r_znear->value;
	

	vec4_t quadVerts[4];
	vec2_t texCoords[4];

	FBO_Bind(tr.quarterFbo[0]);

	qglViewport(0, 0, tr.quarterFbo[0]->width, tr.quarterFbo[0]->height);
	qglScissor(0, 0, tr.quarterFbo[0]->width, tr.quarterFbo[0]->height);

	VectorSet4(quadVerts[0], -1, 1, 0, 1);
	VectorSet4(quadVerts[1], 1, 1, 0, 1);
	VectorSet4(quadVerts[2], 1, -1, 0, 1);
	VectorSet4(quadVerts[3], -1, -1, 0, 1);

	texCoords[0][0] = 0; texCoords[0][1] = 1;
	texCoords[1][0] = 1; texCoords[1][1] = 1;
	texCoords[2][0] = 1; texCoords[2][1] = 0;
	texCoords[3][0] = 0; texCoords[3][1] = 0;

	GL_State(GLS_DEPTHTEST_DISABLE);

	GLSL_BindProgram(&tr.ssaoShader);

	GL_BindToTMU(tr.hdrDepthImage, TB_COLORMAP);

	matrix_t invProjectionMatrix;
	Matrix16Inverse(backEnd.viewParms.projectionMatrix, invProjectionMatrix);

	GLSL_SetUniformMatrix4x4(&tr.ssaoShader, UNIFORM_INVVIEWPROJECTIONMATRIX, invProjectionMatrix);
	vec4_t viewInfo = { zmax / zmin, zmax, 0.0f, 0.0f };
	GLSL_SetUniformVec4(&tr.ssaoShader, UNIFORM_VIEWINFO, viewInfo);

	qglDrawArrays(GL_TRIANGLES, 0, 3);

	FBO_Bind(tr.quarterFbo[1]);
	GLSL_BindProgram(&tr.depthBlurShader[0]);
	GL_BindToTMU(tr.quarterImage[0], TB_COLORMAP);
	GL_BindToTMU(tr.hdrDepthImage, TB_LIGHTMAP);
	VectorSet4(viewInfo, zmax / zmin, zmax, 2.0f, -2.0f );
	GLSL_SetUniformVec4(&tr.depthBlurShader[0], UNIFORM_VIEWINFO, viewInfo);
	qglDrawArrays(GL_TRIANGLES, 0, 3);

	FBO_Bind(tr.quarterFbo[0]);
	GL_BindToTMU(tr.quarterImage[1], TB_COLORMAP);
	VectorSet4(viewInfo, zmax / zmin, zmax, 2.0f, 2.0f);
	GLSL_SetUniformVec4(&tr.depthBlurShader[0], UNIFORM_VIEWINFO, viewInfo);
	qglDrawArrays(GL_TRIANGLES, 0, 3);

	FBO_Bind(tr.quarterFbo[1]);
	GL_BindToTMU(tr.quarterImage[0], TB_COLORMAP);
	VectorSet4(viewInfo, zmax / zmin, zmax, 1.0f, 0.0f);
	GLSL_SetUniformVec4(&tr.depthBlurShader[0], UNIFORM_VIEWINFO, viewInfo);
	qglDrawArrays(GL_TRIANGLES, 0, 3);

	GLSL_BindProgram(&tr.depthBlurShader[0]);

	FBO_Bind(tr.screenSsaoFbo);
	GL_BindToTMU(tr.quarterImage[1], TB_COLORMAP);
	GL_BindToTMU(tr.hdrDepthImage, TB_LIGHTMAP);
	VectorSet4(viewInfo, zmax / zmin, zmax, 0.0f, 1.0f);
	GLSL_SetUniformVec4(&tr.depthBlurShader[0], UNIFORM_VIEWINFO, viewInfo);
	qglDrawArrays(GL_TRIANGLES, 0, 3);
}

static void RB_RenderDepthOnly(drawSurf_t *drawSurfs, int numDrawSurfs)
{
	backEnd.renderPass = (backEnd.viewParms.flags & VPF_DEPTHSHADOW) ? DEPTH_PASS : PRE_PASS;

	if (backEnd.renderPass == DEPTH_PASS)
		qglColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	RB_RenderDrawSurfList(drawSurfs, numDrawSurfs);

	if (backEnd.renderPass == DEPTH_PASS)
		qglColorMask(
			!backEnd.colorMask[0],
			!backEnd.colorMask[1],
			!backEnd.colorMask[2],
			!backEnd.colorMask[3]);

	if (tr.renderCubeFbo != NULL && backEnd.viewParms.targetFbo == tr.renderCubeFbo && tr.msaaResolveFbo)
	{
		// If we're using multisampling and rendering a cubemap, resolve the depth to correct size first
		vec4i_t frameBox;
		frameBox[0] = backEnd.viewParms.viewportX;
		frameBox[1] = backEnd.viewParms.viewportY;
		frameBox[2] = backEnd.viewParms.viewportWidth;
		frameBox[3] = backEnd.viewParms.viewportHeight;
		FBO_FastBlit(
			tr.renderCubeFbo, frameBox,
			tr.msaaResolveFbo, frameBox,
			GL_DEPTH_BUFFER_BIT,
			GL_NEAREST);
	}
	else if (tr.msaaResolveFbo)
	{
		if (backEnd.renderPass == PRE_PASS)
		{
			FBO_FastBlit(tr.preBuffersFbo, NULL, tr.msaaPreResolveFbo, NULL, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
			FBO_FastBlitIndexed(tr.preBuffersFbo, tr.msaaPreResolveFbo, 1, 1, GL_COLOR_BUFFER_BIT, GL_NEAREST);
			if (r_ssr->integer)
				FBO_FastBlitIndexed(tr.preBuffersFbo, tr.msaaPreResolveFbo, 2, 2, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
		else
			// If we're using multisampling, resolve the depth first
			FBO_FastBlit(
				tr.renderFbo, NULL,
				tr.msaaResolveFbo, NULL,
				GL_DEPTH_BUFFER_BIT,
				GL_NEAREST);

	}
	else if (tr.renderFbo == NULL)
	{
		// If we're rendering directly to the screen, copy the depth to a texture
		GL_BindToTMU(tr.renderDepthImage, 0);
		qglCopyTexImage2D(
			GL_TEXTURE_2D, 0,
			GL_DEPTH_COMPONENT24, 0,
			0, glConfig.vidWidth,
			glConfig.vidHeight, 0);
	}

	backEnd.renderPass = MAIN_PASS;
}

static void RB_RenderMainPass(drawSurf_t *drawSurfs, int numDrawSurfs)
{
	if (backEnd.viewParms.flags & VPF_DEPTHSHADOW)
	{
		return;
	}

	if (tr.world)
	{
		GL_BindToTMU(tr.envBrdfImage, TB_ENVBRDFMAP);
		GL_BindToTMU(tr.world->ambientLightImages[0], TB_LGAMBIENT);
		GL_BindToTMU(tr.world->directionImages, TB_LGDIRECTION);
		GL_BindToTMU(tr.world->directionalLightImages[0], TB_LGLIGHTCOLOR);
	}
	else
	{
		GL_BindToTMU(tr.envBrdfImage, TB_ENVBRDFMAP);
		GL_BindToTMU(tr.defaultAmbientLight, TB_LGAMBIENT);
		GL_BindToTMU(tr.defaultLightVec, TB_LGDIRECTION);
		GL_BindToTMU(tr.defaultDirectLight, TB_LGLIGHTCOLOR);
	}

	RB_RenderDrawSurfList(drawSurfs, numDrawSurfs);

	if (r_drawSun->integer)
	{
		RB_DrawSun(0.1f, tr.sunShader);
	}

	if (r_drawSunRays->integer)
	{
		FBO_t *oldFbo = glState.currentFBO;
		FBO_Bind(tr.sunRaysFbo);

		qglClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		qglClear(GL_COLOR_BUFFER_BIT);

		tr.sunFlareQueryActive[tr.sunFlareQueryIndex] = qtrue;
		qglBeginQuery(GL_SAMPLES_PASSED, tr.sunFlareQuery[tr.sunFlareQueryIndex]);

		RB_DrawSun(0.3f, tr.sunFlareShader);

		qglEndQuery(GL_SAMPLES_PASSED);

		FBO_Bind(oldFbo);
	}

	// darken down any stencil shadows
	RB_ShadowFinish();

	// add light flares on lights that aren't obscured
	RB_RenderFlares();
}

static void RB_RenderAllDepthRelatedPasses(drawSurf_t *drawSurfs, int numDrawSurfs)
{
	if (backEnd.refdef.rdflags & RDF_NOWORLDMODEL)
	{
		return;
	}

	if (!r_depthPrepass->integer && !(backEnd.viewParms.flags & VPF_DEPTHSHADOW))
	{
		return;
	}

	FBO_t *oldFbo = glState.currentFBO;

	if (backEnd.viewParms.flags & VPF_DEPTHCLAMP)
	{
		qglEnable(GL_DEPTH_CLAMP);
	}

	RB_RenderDepthOnly(drawSurfs, numDrawSurfs);

	if (r_ssao->integer)
	{
		// need the depth in a texture we can do GL_LINEAR sampling on, so
		// copy it to an HDR image
		FBO_BlitFromTexture(
			tr.renderDepthImage,
			nullptr,
			nullptr,
			tr.hdrDepthFbo,
			nullptr,
			nullptr,
			nullptr, 0);
	}

	if (r_sunlightMode->integer && (backEnd.viewParms.flags & VPF_USESUNLIGHT))
	{
		RB_RenderSunShadows();
	}

	if (r_ssao->integer)
	{
		RB_RenderSSAO();
	}

	// reset viewport and scissor
	FBO_Bind(oldFbo);
	SetViewportAndScissor();

	if (backEnd.viewParms.flags & VPF_DEPTHCLAMP)
	{
		qglDisable(GL_DEPTH_CLAMP);
	}
}

void RB_RenderAllRealTimeLightTypes()
{
	FBO_t *fbo = glState.currentFBO;
	// clear all content of lighting buffers
	FBO_Bind(tr.preLightFbo[PRELIGHT_DIFFUSE_SPECULAR_FBO]);
	qglClearColor(0.f, 0.f, 0.f, 1.0f);
	qglClear(GL_COLOR_BUFFER_BIT);

	if ((backEnd.viewParms.flags & VPF_DEPTHSHADOW) ||
		(backEnd.refdef.rdflags & RDF_NOWORLDMODEL) ||
		(backEnd.refdef.rdflags & RDF_SKYBOXPORTAL) ||
		(tr.shadowCubeFbo != NULL && backEnd.viewParms.targetFbo == tr.shadowCubeFbo))
	{
		FBO_Bind(fbo);
		return;
	}

	const float zmax = backEnd.viewParms.zFar;
	const float zmin = backEnd.viewParms.zNear;
	vec4_t viewInfo = { zmax / zmin, zmax, zmin, 0.0f };

	GL_DepthRange(0.0, 1.0);

	const float ymax = zmax * tanf(backEnd.viewParms.fovY * M_PI / 360.0f);
	const float xmax = zmax * tanf(backEnd.viewParms.fovX * M_PI / 360.0f);

	vec3_t viewBasis[3];
	VectorScale(backEnd.refdef.viewaxis[0], zmax, viewBasis[0]);
	VectorScale(backEnd.refdef.viewaxis[1], xmax, viewBasis[1]);
	VectorScale(backEnd.refdef.viewaxis[2], ymax, viewBasis[2]);

	matrix_t viewProjectionMatrix;
	Matrix16Multiply(
		backEnd.viewParms.projectionMatrix,
		backEnd.viewParms.world.modelViewMatrix,
		viewProjectionMatrix);
	
	GL_BindToTMU(tr.renderDepthImage, 1);
	GL_BindToTMU(tr.normalBufferImage, 2);
	GL_BindToTMU(tr.specBufferImage, 3);
	GL_BindToTMU(NULL, 4);

	if (!((tr.buildingSphericalHarmonics) ||
		(tr.renderCubeFbo != NULL && backEnd.viewParms.targetFbo == tr.renderCubeFbo)) &&
		r_ssr->integer &&
		!backEnd.viewParms.isPortal
		)
	{
		GL_BindToTMU(tr.prevRenderDepthImage, 6);
		GL_BindToTMU(tr.prevRenderImage, 0);
		GL_BindToTMU(tr.randomImage, 11);

		FBO_Bind(tr.preLightFbo[PRELIGHT_PRE_SSR_FBO]);
		qglClear(GL_COLOR_BUFFER_BIT);

		GLSL_VertexAttribsState(ATTR_POSITION, NULL);
		GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO | GLS_DEPTHTEST_DISABLE);
		GL_Cull(CT_FRONT_SIDED);

		int index = PRELIGHT_SSR;
		shaderProgram_t *sp = &tr.prelightShader[index];
		GLSL_BindProgram(sp);

		GLSL_SetUniformVec3(sp, UNIFORM_VIEWFORWARD, viewBasis[0]);
		GLSL_SetUniformVec3(sp, UNIFORM_VIEWLEFT, viewBasis[1]);
		GLSL_SetUniformVec3(sp, UNIFORM_VIEWUP, viewBasis[2]);
		GLSL_SetUniformVec3(sp, UNIFORM_VIEWORIGIN, backEnd.viewParms.ori.origin);
		const float samples = 64.0f;
		vec4_t viewInfo = { tr.viewParms.zNear, tr.viewParms.zFar, (float)(tr.frameCount % 8), Q_flrand(samples / 2.0f, samples) };
		GLSL_SetUniformVec4(sp, UNIFORM_VIEWINFO, viewInfo);
		
		matrix_t invModelViewMatrix;
		matrix_t transInvModelViewMatrix;
		Matrix16Inverse(backEnd.viewParms.world.modelViewMatrix, invModelViewMatrix);
		Matrix16Transpose(invModelViewMatrix, transInvModelViewMatrix);
		
		matrix_t invProjectionMatrix;
		Matrix16Inverse(backEnd.viewParms.projectionMatrix, invProjectionMatrix);

		GLSL_SetUniformMatrix4x4(sp, UNIFORM_INVVIEWPROJECTIONMATRIX, invProjectionMatrix);

		GLSL_SetUniformMatrix4x4(sp, UNIFORM_MODELMATRIX, backEnd.viewParms.projectionMatrix);
		GLSL_SetUniformMatrix4x4(sp, UNIFORM_NORMALMATRIX, transInvModelViewMatrix);
		GLSL_SetUniformMatrix4x4(sp, UNIFORM_MODELVIEWPROJECTIONMATRIX, backEnd.viewParms.world.modelViewMatrix);
		qglViewport(0, 0, tr.preSSRImage[0]->width , tr.preSSRImage[0]->height);
		qglScissor(0, 0, tr.preSSRImage[0]->width, tr.preSSRImage[0]->height);

		qglDrawArrays(GL_TRIANGLES, 0, 3);
		qglViewport(0, 0, tr.renderImage->width, tr.renderImage->height);
		qglScissor(0, 0, tr.renderImage->width, tr.renderImage->height);
		
		// ssr resolve
		FBO_Bind(tr.preLightFbo[PRELIGHT_RESOLVE_FBO]);

		GL_BindToTMU(tr.prevRenderImage, 0);
		GL_BindToTMU(tr.renderDepthImage, 1);
		GL_BindToTMU(tr.preSSRImage[0], 4);
		GL_BindToTMU(tr.preSSRImage[1], 5);
		GL_BindToTMU(tr.velocityImage, 6);

		index = PRELIGHT_SSR_RESOLVE;
		sp = &tr.prelightShader[index];
		GLSL_BindProgram(sp);

		GLSL_SetUniformVec3(sp, UNIFORM_VIEWFORWARD, viewBasis[0]);
		GLSL_SetUniformVec3(sp, UNIFORM_VIEWLEFT, viewBasis[1]);
		GLSL_SetUniformVec3(sp, UNIFORM_VIEWUP, viewBasis[2]);
		GLSL_SetUniformVec3(sp, UNIFORM_VIEWORIGIN, backEnd.viewParms.ori.origin);

		VectorSet4(viewInfo, 1.f / (float)tr.renderImage->width, 1.f / (float)tr.renderImage->height, Q_flrand(0.f, 8.f), cos(Q_flrand(0.f, 360.f)));
		GLSL_SetUniformVec4(sp, UNIFORM_VIEWINFO, viewInfo);
		
		GLSL_SetUniformMatrix4x4(sp, UNIFORM_INVVIEWPROJECTIONMATRIX, invProjectionMatrix);
		GLSL_SetUniformMatrix4x4(sp, UNIFORM_MODELMATRIX, backEnd.viewParms.projectionMatrix);
		GLSL_SetUniformMatrix4x4(sp, UNIFORM_NORMALMATRIX, transInvModelViewMatrix);
		GLSL_SetUniformMatrix4x4(sp, UNIFORM_MODELVIEWPROJECTIONMATRIX, backEnd.viewParms.world.modelViewMatrix);

		qglDrawArrays(GL_TRIANGLES, 0, 3);

		// temporal filter
		bool oddFrame = tr.frameCount % 2 == 1;
		FBO_Bind(tr.preLightFbo[oddFrame ? PRELIGHT_TEMP_ODD_FBO : PRELIGHT_TEMP_EVEN_FBO]);
		
		GL_BindToTMU(tr.resolveImage, 0);
		GL_BindToTMU(oddFrame ? tr.tempFilterEvenBufferImage : tr.tempFilterOddBufferImage, 1);

		GL_BindToTMU(tr.preSSRImage[0], 4);
		GL_BindToTMU(tr.preSSRImage[1], 5);
		GL_BindToTMU(tr.velocityImage, 6);

		index = PRELIGHT_TEMPORAL_FILTER;
		sp = &tr.prelightShader[index];
		GLSL_BindProgram(sp);

		GLSL_SetUniformVec3(sp, UNIFORM_VIEWFORWARD, viewBasis[0]);
		GLSL_SetUniformVec3(sp, UNIFORM_VIEWLEFT, viewBasis[1]);
		GLSL_SetUniformVec3(sp, UNIFORM_VIEWUP, viewBasis[2]);
		GLSL_SetUniformVec3(sp, UNIFORM_VIEWORIGIN, backEnd.viewParms.ori.origin);

		if (tr.envBrdfImage != NULL)
			GL_BindToTMU(tr.envBrdfImage, 7);

		qglDrawArrays(GL_TRIANGLES, 0, 3);

		GL_BindToTMU(NULL, 4);
		GL_BindToTMU(NULL, 5);
	}

	//render cubemaps where SSR  didn't render
	//buggy! don't enable for now, finish when cubemap arrarys or bindless textures are available
	int numCubemaps = tr.numCubemaps;
	if (0)//(r_cubeMapping->integer && !(tr.viewParms.flags & VPF_NOCUBEMAPS) && numCubemaps);
	{
		FBO_Bind(tr.preLightFbo[PRELIGHT_SPECULAR_FBO]);

		vec4_t cubemapTransforms[MAX_DLIGHTS];

		tess.useInternalVBO = qfalse;
		R_BindVBO(tr.lightSphereVolume.vbo);
		R_BindIBO(tr.lightSphereVolume.ibo);
		GLSL_VertexAttribsState(ATTR_POSITION, NULL);
		GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_GREATER);
		GL_Cull(CT_BACK_SIDED);

		for (int i = 0; i < numCubemaps; i++)
		{
			cubemap_t *cubemap = &tr.cubemaps[i];

			cubemapTransforms[i][0] = cubemap->origin[0];
			cubemapTransforms[i][1] = cubemap->origin[1];
			cubemapTransforms[i][2] = cubemap->origin[2];
			cubemapTransforms[i][3] = cubemap->parallaxRadius;
		}

		int index = PRELIGHT_CUBEMAP;
		shaderProgram_t *sp = &tr.prelightShader[index];
		GLSL_BindProgram(sp);

		GLSL_SetUniformVec3(sp, UNIFORM_VIEWFORWARD, viewBasis[0]);
		GLSL_SetUniformVec3(sp, UNIFORM_VIEWLEFT, viewBasis[1]);
		GLSL_SetUniformVec3(sp, UNIFORM_VIEWUP, viewBasis[2]);
		GLSL_SetUniformVec4(sp, UNIFORM_VIEWINFO, viewInfo);
		GLSL_SetUniformVec3(sp, UNIFORM_VIEWORIGIN, backEnd.viewParms.ori.origin);
		GLSL_SetUniformMatrix4x4(sp, UNIFORM_MODELVIEWPROJECTIONMATRIX, viewProjectionMatrix);
		
		matrix_t invProjectionMatrix;
		Matrix16Inverse(viewProjectionMatrix, invProjectionMatrix);

		GLSL_SetUniformMatrix4x4(sp, UNIFORM_INVVIEWPROJECTIONMATRIX, invProjectionMatrix);

		GLSL_SetUniformInt(sp, UNIFORM_NUMCUBEMAPS, numCubemaps);
		GLSL_SetUniformVec4N(sp, UNIFORM_CUBEMAPTRANSFORMS, (float*)cubemapTransforms, numCubemaps);

		for (int i = 0; i < numCubemaps / 4.0f; i++) {

			//offset
			GLSL_SetUniformInt(sp, UNIFORM_VERTOFFSET, i * 4);

			int rest = (numCubemaps - 4 * i > 3) ? 4 : (numCubemaps - 4 * i);

			if (rest > 0)
				GL_BindToTMU(tr.cubemaps[i * 4 + 0].image, 4);
			if (rest > 1)
				GL_BindToTMU(tr.cubemaps[i * 4 + 1].image, 5);
			if (rest > 2)
				GL_BindToTMU(tr.cubemaps[i * 4 + 2].image, 6);
			if (rest > 3)
				GL_BindToTMU(tr.cubemaps[i * 4 + 3].image, 8);

			if (tr.envBrdfImage != NULL)
				GL_BindToTMU(tr.envBrdfImage, 7);

			qglDrawElementsInstanced(GL_TRIANGLES, tr.lightSphereVolume.numIndexes, GL_UNSIGNED_INT, 0, rest);
		}
	}

	//render sun lights or maybe not, can also be rendered forward, shouldn't make a difference, only invest time when multiple suns are needed
	if (0)//(r_sunlightMode->integer)
	{
		FBO_Bind(tr.preLightFbo[PRELIGHT_DIFFUSE_SPECULAR_FBO]);

		tess.useInternalVBO = qfalse;
		R_BindVBO(tr.screenQuad.vbo);
		R_BindIBO(tr.screenQuad.ibo);
		GLSL_VertexAttribsState(ATTR_POSITION, NULL);
		GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHTEST_DISABLE);
		GL_Cull(CT_FRONT_SIDED);

		int index = PRELIGHT_SUN_LIGHT;
		shaderProgram_t *sp = &tr.prelightShader[index];
		GLSL_BindProgram(sp);

		GLSL_SetUniformVec3(sp, UNIFORM_VIEWFORWARD, viewBasis[0]);
		GLSL_SetUniformVec3(sp, UNIFORM_VIEWLEFT, viewBasis[1]);
		GLSL_SetUniformVec3(sp, UNIFORM_VIEWUP, viewBasis[2]);
		GLSL_SetUniformVec4(sp, UNIFORM_VIEWINFO, viewInfo);
		GLSL_SetUniformVec3(sp, UNIFORM_VIEWORIGIN, backEnd.viewParms.ori.origin);
		GLSL_SetUniformMatrix4x4(sp, UNIFORM_MODELVIEWPROJECTIONMATRIX, viewProjectionMatrix);

		GLSL_SetUniformVec3(sp, UNIFORM_PRIMARYLIGHTAMBIENT, backEnd.refdef.sunAmbCol);
		GLSL_SetUniformVec3(sp, UNIFORM_PRIMARYLIGHTCOLOR, backEnd.refdef.sunCol);
		GLSL_SetUniformVec4(sp, UNIFORM_PRIMARYLIGHTORIGIN, backEnd.refdef.sunDir);

		GL_BindToTMU(tr.screenShadowImage, 4);

		qglDrawElementsInstanced(GL_TRIANGLES, tr.screenQuad.numIndexes, GL_UNSIGNED_INT, 0, 1);
	}

	//render analytical lights
	int numDlights = backEnd.refdef.num_dlights;
	if (numDlights)
	{
		FBO_Bind(tr.preLightFbo[PRELIGHT_DIFFUSE_SPECULAR_FBO]);
		GL_BindToTMU(tr.renderDepthImage, 1);
		
		vec4_t dlightTransforms[MAX_DLIGHTS];
		vec3_t dlightColors[MAX_DLIGHTS];

		tess.useInternalVBO = qfalse;
		R_BindVBO(tr.lightSphereVolume.vbo);
		R_BindIBO(tr.lightSphereVolume.ibo);
		GLSL_VertexAttribsState(ATTR_POSITION, NULL);
		GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_GREATER);
		GL_Cull(CT_BACK_SIDED);

		for (int i = 0; i < numDlights; i++)
		{
			dlight_t *dlight = backEnd.refdef.dlights + i;

			dlightTransforms[i][0] = dlight->origin[0];
			dlightTransforms[i][1] = dlight->origin[1];
			dlightTransforms[i][2] = dlight->origin[2];
			dlightTransforms[i][3] = dlight->radius;

			dlightColors[i][0] = dlight->color[0];
			dlightColors[i][1] = dlight->color[1];
			dlightColors[i][2] = dlight->color[2];
		}

		//TODO: write support for other light types like spot lights and tube lights
		int index = PRELIGHT_POINT_LIGHT;
		shaderProgram_t *sp = &tr.prelightShader[index];
		GLSL_BindProgram(sp);

		GLSL_SetUniformVec3(sp, UNIFORM_VIEWFORWARD, viewBasis[0]);
		GLSL_SetUniformVec3(sp, UNIFORM_VIEWLEFT, viewBasis[1]);
		GLSL_SetUniformVec3(sp, UNIFORM_VIEWUP, viewBasis[2]);
		GLSL_SetUniformVec4(sp, UNIFORM_VIEWINFO, viewInfo);
		GLSL_SetUniformVec3(sp, UNIFORM_VIEWORIGIN, backEnd.viewParms.ori.origin);
		GLSL_SetUniformMatrix4x4(sp, UNIFORM_MODELVIEWPROJECTIONMATRIX, viewProjectionMatrix);

		matrix_t invProjectionMatrix;
		Matrix16Inverse(viewProjectionMatrix, invProjectionMatrix);
		GLSL_SetUniformMatrix4x4(sp, UNIFORM_INVVIEWPROJECTIONMATRIX, invProjectionMatrix);

		GLSL_SetUniformVec3N(sp, UNIFORM_LIGHTCOLORS, (float*)dlightColors, numDlights);
		GLSL_SetUniformVec4N(sp, UNIFORM_LIGHTTRANSFORMS, (float*)dlightTransforms, numDlights);

		for (int i = 0; i < numDlights / 4.0f; i++) {

			//offset
			GLSL_SetUniformInt(sp, UNIFORM_VERTOFFSET, i * 4);

			int rest = (numDlights - 4 * i > 3) ? 4 : (numDlights - 4 * i);

			if (r_dlightMode->integer > 1) {
				GL_BindToTMU(tr.shadowCubemaps[i * 4 + 0].image, 6);
				GL_BindToTMU(tr.shadowCubemaps[i * 4 + 1].image, 8);
				GL_BindToTMU(tr.shadowCubemaps[i * 4 + 2].image, 9);
				GL_BindToTMU(tr.shadowCubemaps[i * 4 + 3].image, 10);
			}
			else {
				GL_BindToTMU(tr.whiteImage, 6);
				GL_BindToTMU(tr.whiteImage, 8);
				GL_BindToTMU(tr.whiteImage, 9);
				GL_BindToTMU(tr.whiteImage, 10);
			}
			qglDrawElementsInstanced(GL_TRIANGLES, tr.lightSphereVolume.numIndexes, GL_UNSIGNED_INT, 0, rest);
		}
	}

	FBO_Bind(fbo);
}

/*
=============
RB_DrawSurfs

=============
*/
static const void	*RB_DrawSurfs( const void *data ) {
	const drawSurfsCommand_t	*cmd;

	// finish any 2D drawing if needed
	if (tess.numIndexes) {
		RB_EndSurface();
	}

	cmd = (const drawSurfsCommand_t *)data;

	backEnd.refdef = cmd->refdef;
	backEnd.viewParms = cmd->viewParms;

	// clear the z buffer, set the modelview, etc
	RB_BeginDrawingView();

	RB_RenderAllDepthRelatedPasses(cmd->drawSurfs, cmd->numDrawSurfs);

	RB_BuildHiZBuffer();

	RB_RenderAllRealTimeLightTypes();

	RB_RenderMainPass(cmd->drawSurfs, cmd->numDrawSurfs);

	return (const void *)(cmd + 1);
}

/*
=============
RB_DrawBuffer

=============
*/
static const void	*RB_DrawBuffer( const void *data ) {
	const drawBufferCommand_t	*cmd;

	cmd = (const drawBufferCommand_t *)data;

	// finish any 2D drawing if needed
	if(tess.numIndexes)
		RB_EndSurface();

	return (const void *)(cmd + 1);
}

/*
===============
RB_ShowImages

Draw all the images to the screen, on top of whatever
was there.  This is used to test for texture thrashing.

Also called by RE_EndRegistration
===============
*/
void RB_ShowImages( void ) {
	int		i;
	image_t	*image;
	float	x, y, w, h;
	int		start, end;

	RB_SetGL2D();

	qglClear( GL_COLOR_BUFFER_BIT );

	qglFinish();

	start = ri.Milliseconds();

	for (i = 0; i<tr.numImages; i++) {
		image = tr.images[i];

		w = glConfig.vidWidth / 20;
		h = glConfig.vidHeight / 15;
		x = i % 20 * w;
		y = i / 20 * h;

		// show in proportional size in mode 2
		if ( r_showImages->integer == 2 ) {
			w *= image->uploadWidth / 512.0f;
			h *= image->uploadHeight / 512.0f;
		}

		{
			vec4_t quadVerts[4];

			GL_Bind(image);

			VectorSet4(quadVerts[0], x, y, 0, 1);
			VectorSet4(quadVerts[1], x + w, y, 0, 1);
			VectorSet4(quadVerts[2], x + w, y + h, 0, 1);
			VectorSet4(quadVerts[3], x, y + h, 0, 1);

			RB_InstantQuad(quadVerts);
		}
	}

	qglFinish();

	end = ri.Milliseconds();
	ri.Printf( PRINT_ALL, "%i msec to draw all images\n", end - start );

}

/*
=============
RB_ColorMask

=============
*/
static const void *RB_ColorMask(const void *data)
{
	const colorMaskCommand_t *cmd = (colorMaskCommand_t *)data;

	// finish any 2D drawing if needed
	RB_EndSurface();

	// reverse color mask, so 0 0 0 0 is the default
	backEnd.colorMask[0] = (qboolean)(!cmd->rgba[0]);
	backEnd.colorMask[1] = (qboolean)(!cmd->rgba[1]);
	backEnd.colorMask[2] = (qboolean)(!cmd->rgba[2]);
	backEnd.colorMask[3] = (qboolean)(!cmd->rgba[3]);

	qglColorMask(cmd->rgba[0], cmd->rgba[1], cmd->rgba[2], cmd->rgba[3]);
	
	return (const void *)(cmd + 1);
}

/*
=============
RB_ClearDepth

=============
*/
static const void *RB_ClearDepth(const void *data)
{
	const clearDepthCommand_t *cmd = (clearDepthCommand_t *)data;
	
	// finish any 2D drawing if needed
	if(tess.numIndexes)
		RB_EndSurface();

	// texture swapping test
	if (r_showImages->integer)
		RB_ShowImages();

	if (!tr.renderFbo || backEnd.framePostProcessed)
	{
		FBO_Bind(NULL);
	}
	else
	{
		FBO_Bind(tr.renderFbo);
	}

	qglClear(GL_DEPTH_BUFFER_BIT);

	// if we're doing MSAA, clear the depth texture for the resolve buffer
	if (tr.msaaResolveFbo)
	{
		FBO_Bind(tr.msaaResolveFbo);
		qglClear(GL_DEPTH_BUFFER_BIT);
	}

	
	return (const void *)(cmd + 1);
}


/*
=============
RB_SwapBuffers

=============
*/
static const void	*RB_SwapBuffers( const void *data ) {
	const swapBuffersCommand_t	*cmd;

	// finish any 2D drawing if needed
	if ( tess.numIndexes ) {
		RB_EndSurface();
	}

	// texture swapping test
	if ( r_showImages->integer ) {
		RB_ShowImages();
	}

	cmd = (const swapBuffersCommand_t *)data;

	// we measure overdraw by reading back the stencil buffer and
	// counting up the number of increments that have happened
	if ( r_measureOverdraw->integer ) {
		int i;
		long sum = 0;
		unsigned char *stencilReadback;

		stencilReadback = (unsigned char *)R_Malloc(glConfig.vidWidth * glConfig.vidHeight, TAG_TEMP_WORKSPACE, qfalse);
		qglReadPixels( 0, 0, glConfig.vidWidth, glConfig.vidHeight, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, stencilReadback );

		for ( i = 0; i < glConfig.vidWidth * glConfig.vidHeight; i++ ) {
			sum += stencilReadback[i];
		}

		backEnd.pc.c_overDraw += sum;
		R_Free( stencilReadback );
	}

	if (!backEnd.framePostProcessed)
	{
		if (tr.msaaResolveFbo && r_hdr->integer)
		{
			// Resolving an RGB16F MSAA FBO to the screen messes with the brightness, so resolve to an RGB16F FBO first
			FBO_FastBlit(tr.renderFbo, NULL, tr.msaaResolveFbo, NULL, GL_COLOR_BUFFER_BIT, GL_NEAREST);
			FBO_FastBlit(tr.msaaResolveFbo, NULL, NULL, NULL, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
		else if (tr.renderFbo)
		{
			FBO_FastBlit(tr.renderFbo, NULL, NULL, NULL, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
	}

	if ( tr.numFramesToCapture > 0 )
	{
		tr.numFramesToCapture--;
		if ( !tr.numFramesToCapture )
		{
			ri.Printf( PRINT_ALL, "Frames captured\n" );
			ri.FS_FCloseFile(tr.debugFile);
			tr.debugFile = 0;
		}
	}

	int frameNumber = backEndData->realFrameNumber;
	gpuFrame_t *currentFrame = backEndData->currentFrame;

	assert( !currentFrame->sync );
	currentFrame->sync = qglFenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE, 0 );

	backEndData->realFrameNumber = frameNumber + 1;

	GLimp_LogComment( "***************** RB_SwapBuffers *****************\n\n\n" );

	ri.WIN_Present( &window );

	backEnd.framePostProcessed = qfalse;
	backEnd.projection2D = qfalse;

	return (const void *)(cmd + 1);
}

void RB_StoreFrameData() {

	if ((backEnd.viewParms.flags & VPF_DEPTHSHADOW) ||
		(backEnd.refdef.rdflags & RDF_NOWORLDMODEL) ||
		(backEnd.refdef.rdflags & RDF_SKYBOXPORTAL) ||
		(tr.renderCubeFbo != NULL && backEnd.viewParms.targetFbo == tr.renderCubeFbo) ||
		(tr.shadowCubeFbo != NULL && backEnd.viewParms.targetFbo == tr.shadowCubeFbo) &&
		!backEnd.viewParms.isPortal)
		return;

	// finish any 2D drawing if needed
	if (tess.numIndexes) {
		RB_EndSurface();
	}

	R_IssuePendingRenderCommands();

	RB_SetGL2D();

	//store viewProjectionMatrix for reprojecting
	Matrix16Multiply(backEnd.viewParms.projectionMatrix, backEnd.viewParms.world.modelViewMatrix, tr.preViewProjectionMatrix);

	// build blured image buffer for ssr
	int width = tr.renderImage->width;
	int height = tr.renderImage->height;

	vec4_t color = { 1.0f, 1.0f, 1.0f, 1.0f };
	vec2_t texRes = { 1.0f / (float)width, 1.0f / (float)height };
	vec4_t viewInfo;

	GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO | GLS_DEPTHTEST_DISABLE);
	GL_Cull(CT_FRONT_SIDED);

	for (int level = 1; level <= 4; level++) {

		width = width / 2.0;
		height = height / 2.0;

		VectorSet2(texRes, 1.0f / (float)width, 1.0f / (float)height);
		VectorSet4(viewInfo, level - 1, 0.0, 0.0, 0.0);
		
		FBO_Bind(tr.refractiveFbo);
		qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tr.prevRenderImage->texnum, level);
		GLSL_BindProgram(&tr.gaussianBlurShader[0]);
		qglViewport(0, 0, width, height);
		qglScissor(0, 0, width, height);

		GL_BindToTMU(tr.prevRenderImage, TB_COLORMAP);
		GLSL_SetUniformVec4(&tr.gaussianBlurShader[0], UNIFORM_COLOR, color);
		GLSL_SetUniformVec2(&tr.gaussianBlurShader[0], UNIFORM_INVTEXRES, texRes);
		GLSL_SetUniformVec4(&tr.gaussianBlurShader[0], UNIFORM_VIEWINFO, viewInfo);

		qglDrawArrays(GL_TRIANGLES, 0, 3);
	}

	FBO_Bind(tr.refractiveFbo);
	qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tr.prevRenderImage->texnum, 0);

	qglViewport(0, 0, tr.renderImage->width, tr.renderImage->height);
	qglScissor(0, 0, tr.renderImage->width, tr.renderImage->height);
}

/*
=============
RB_PostProcess

=============
*/
const void *RB_PostProcess(const void *data)
{
	const postProcessCommand_t *cmd = (const postProcessCommand_t *)data;
	FBO_t *srcFbo;
	vec4i_t srcBox, dstBox;
	qboolean autoExposure;

	// finish any 2D drawing if needed
	if(tess.numIndexes)
		RB_EndSurface();

	if (tr.viewParms.flags & VPF_NOPOSTPROCESS)
	{
		// do nothing
		return (const void *)(cmd + 1);
	}

	if (cmd)
	{
		backEnd.refdef = cmd->refdef;
		backEnd.viewParms = cmd->viewParms;
	}

	srcFbo = tr.renderFbo;
	if (tr.msaaResolveFbo)
	{
		// Resolve the MSAA before anything else
		// Can't resolve just part of the MSAA FBO, so multiple views will suffer a performance hit here
		FBO_FastBlit(tr.renderFbo, NULL, tr.msaaResolveFbo, NULL, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
		srcFbo = tr.msaaResolveFbo;

		if ( r_dynamicGlow->integer )
		{
			FBO_FastBlitIndexed(tr.renderFbo, tr.msaaResolveFbo, 1, 1, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
		}
	}

	dstBox[0] = backEnd.viewParms.viewportX;
	dstBox[1] = backEnd.viewParms.viewportY;
	dstBox[2] = backEnd.viewParms.viewportWidth;
	dstBox[3] = backEnd.viewParms.viewportHeight;

	if (r_ssao->integer)
	{
		srcBox[0] = backEnd.viewParms.viewportX      * tr.screenSsaoImage->width  / (float)glConfig.vidWidth;
		srcBox[1] = backEnd.viewParms.viewportY      * tr.screenSsaoImage->height / (float)glConfig.vidHeight;
		srcBox[2] = backEnd.viewParms.viewportWidth  * tr.screenSsaoImage->width  / (float)glConfig.vidWidth;
		srcBox[3] = backEnd.viewParms.viewportHeight * tr.screenSsaoImage->height / (float)glConfig.vidHeight;

		//FBO_BlitFromTexture(tr.screenSsaoImage, srcBox, NULL, srcFbo, dstBox, NULL, NULL, GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO);
		//srcBox[1] = tr.screenSsaoImage->height - srcBox[1];
		//srcBox[3] = -srcBox[3];

		FBO_Blit(tr.screenSsaoFbo, srcBox, NULL, srcFbo, dstBox, NULL, NULL, GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO);
	}

	if (r_ssr->integer)
		RB_StoreFrameData();

	if (r_dynamicGlow->integer)
	{
		RB_BloomDownscale(tr.glowImage, tr.glowFboScaled[0]);
		int numPasses = Com_Clampi(1, ARRAY_LEN(tr.glowFboScaled), r_dynamicGlowPasses->integer);
		for ( int i = 1; i < numPasses; i++ )
			RB_BloomDownscale(tr.glowFboScaled[i - 1], tr.glowFboScaled[i]);

		for ( int i = numPasses - 2; i >= 0; i-- )
			RB_BloomUpscale(tr.glowFboScaled[i + 1], tr.glowFboScaled[i]);
	}

	srcBox[0] = backEnd.viewParms.viewportX;
	srcBox[1] = backEnd.viewParms.viewportY;
	srcBox[2] = backEnd.viewParms.viewportWidth;
	srcBox[3] = backEnd.viewParms.viewportHeight;

	if (srcFbo)
	{
		if (r_hdr->integer && (r_toneMap->integer || r_forceToneMap->integer))
		{
			autoExposure = (qboolean)(r_autoExposure->integer || r_forceAutoExposure->integer);
			RB_ToneMap(srcFbo, srcBox, NULL, dstBox, autoExposure);
		}
		else if (r_cameraExposure->value == 0.0f)
		{
			FBO_FastBlit(srcFbo, srcBox, NULL, dstBox, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
		else
		{
			vec4_t color;

			color[0] =
			color[1] =
			color[2] = pow(2, r_cameraExposure->value); //exp2(r_cameraExposure->value);
			color[3] = 1.0f;

			FBO_Blit(srcFbo, srcBox, NULL, NULL, dstBox, NULL, color, 0);
		}
	}

	if (r_drawSunRays->integer)
		RB_SunRays(NULL, srcBox, NULL, dstBox);

	if (1)
		RB_BokehBlur(NULL, srcBox, NULL, dstBox, backEnd.refdef.blurFactor);

	if (0 && r_sunlightMode->integer)
	{
		vec4i_t dstBox;
		VectorSet4(dstBox, 0, 0, 128, 128);
		FBO_BlitFromTexture(tr.sunShadowDepthImage[0], NULL, NULL, NULL, dstBox, NULL, NULL, 0);
		VectorSet4(dstBox, 128, 0, 128, 128);
		FBO_BlitFromTexture(tr.sunShadowDepthImage[1], NULL, NULL, NULL, dstBox, NULL, NULL, 0);
		VectorSet4(dstBox, 256, 0, 128, 128);
		FBO_BlitFromTexture(tr.sunShadowDepthImage[2], NULL, NULL, NULL, dstBox, NULL, NULL, 0);
	}

	if (0)
	{
		vec4i_t dstBox;
		VectorSet4(dstBox, 256, glConfig.vidHeight - 256, 256, 256);
		FBO_BlitFromTexture(tr.renderDepthImage, NULL, NULL, NULL, dstBox, NULL, NULL, 0);
		VectorSet4(dstBox, 512, glConfig.vidHeight - 256, 256, 256);
		FBO_BlitFromTexture(tr.screenShadowImage, NULL, NULL, NULL, dstBox, NULL, NULL, 0);
	}

	if (0)
	{
		vec4i_t dstBox;
		VectorSet4(dstBox, 256, glConfig.vidHeight - 256, 256, 256);
		FBO_BlitFromTexture(tr.quarterImage[0], NULL, NULL, NULL, dstBox, NULL, NULL, 0);
		VectorSet4(dstBox, 512, glConfig.vidHeight - 256, 256, 256);
		FBO_BlitFromTexture(tr.quarterImage[1], NULL, NULL, NULL, dstBox, NULL, NULL, 0);
	}

	if (r_debugVisuals->integer)
	{
		vec4i_t dstBox;
		/*VectorSet4(dstBox, 0, glConfig.vidHeight - 256, 256, 256);
		FBO_BlitFromTexture(tr.preSSRImage[0], NULL, NULL, NULL, dstBox, NULL, NULL, 0);
		VectorSet4(dstBox, 0, glConfig.vidHeight - 512, 256, 256);
		FBO_BlitFromTexture(tr.preSSRImage[1], NULL, NULL, NULL, dstBox, NULL, NULL, 0);
		VectorSet4(dstBox, 256, glConfig.vidHeight - 256, 256, 256);
		FBO_BlitFromTexture(tr.prevRenderImage, NULL, NULL, NULL, dstBox, NULL, NULL, 0);
		VectorSet4(dstBox, 256, glConfig.vidHeight - 512, 256, 256);
		FBO_BlitFromTexture(tr.velocityImage, NULL, NULL, NULL, dstBox, NULL, NULL, 0);
		VectorSet4(dstBox, 512, glConfig.vidHeight - 256, 256, 256);
		FBO_BlitFromTexture(tr.specBufferImage, NULL, NULL, NULL, dstBox, NULL, NULL, 0);
		VectorSet4(dstBox, 768, glConfig.vidHeight - 256, 256, 256);
		FBO_BlitFromTexture(tr.diffuseLightingImage, NULL, NULL, NULL, dstBox, NULL, NULL, 0);
		VectorSet4(dstBox, 1024, glConfig.vidHeight - 256, 256, 256);
		FBO_BlitFromTexture(tr.specularLightingImage, NULL, NULL, NULL, dstBox, NULL, NULL, 0);
		VectorSet4(dstBox, 1280, glConfig.vidHeight - 256, 256, 256);
		FBO_BlitFromTexture(tr.normalBufferImage, NULL, NULL, NULL, dstBox, NULL, NULL, 0);*/
		VectorSet4(dstBox, 0, 0, glConfig.vidWidth, glConfig.vidHeight);
		FBO_BlitFromTexture(tr.resolveImage, NULL, NULL, NULL, dstBox, NULL, NULL, 0);
	}

	if (0)
	{
		vec4i_t dstBox;
		VectorSet4(dstBox, 256, glConfig.vidHeight - 256, 256, 256);
		FBO_BlitFromTexture(tr.sunRaysImage, NULL, NULL, NULL, dstBox, NULL, NULL, 0);
	}
	
	/*ri.Printf(PRINT_ALL, " axis_0 %f %f %f \n axis_1 %f %f %f \n axis_2 %f %f %f \n", 
		tr.viewParms.ori.axis[0][0], tr.viewParms.ori.axis[0][1], tr.viewParms.ori.axis[0][2],\
		tr.viewParms.ori.axis[1][0], tr.viewParms.ori.axis[1][1], tr.viewParms.ori.axis[1][2],
		tr.viewParms.ori.axis[2][0], tr.viewParms.ori.axis[2][1], tr.viewParms.ori.axis[2][2]);*/

	if (0)
	{
		vec4i_t dstBox;
		VectorSet4(dstBox, 256, glConfig.vidHeight - 512, 512, 512);
		FBO_BlitFromTexture(tr.weatherDepthImage, NULL, NULL, NULL, dstBox, NULL, NULL, 0);
	}

#if 0
	if (r_cubeMapping->integer && tr.numCubemaps)
	{
		vec4i_t dstBox;
		int cubemapIndex = R_CubemapForPoint( backEnd.viewParms.ori.origin );

		if (cubemapIndex)
		{
			VectorSet4(dstBox, 0, glConfig.vidHeight - 256, 256, 256);
			FBO_BlitFromTexture(tr.prefilterEnvMapImage, NULL, NULL, NULL, dstBox, NULL, NULL, 0);
			//FBO_BlitFromTexture(tr.cubemaps[cubemapIndex - 1].image, NULL, NULL, NULL, dstBox, NULL, NULL, 0);
		}
	}
#endif
	if (0 && r_shadows->integer == 4)
	{
		ivec4_t dstBox;
		VectorSet4(dstBox, 512 + 0, glConfig.vidHeight - 128, 128, 128);
		FBO_BlitFromTexture(tr.pshadowMaps[0], NULL, NULL, NULL, dstBox, NULL, NULL, 0);
		VectorSet4(dstBox, 512 + 128, glConfig.vidHeight - 128, 128, 128);
		FBO_BlitFromTexture(tr.pshadowMaps[1], NULL, NULL, NULL, dstBox, NULL, NULL, 0);
		VectorSet4(dstBox, 512 + 256, glConfig.vidHeight - 128, 128, 128);
		FBO_BlitFromTexture(tr.pshadowMaps[2], NULL, NULL, NULL, dstBox, NULL, NULL, 0);
		VectorSet4(dstBox, 512 + 384, glConfig.vidHeight - 128, 128, 128);
		FBO_BlitFromTexture(tr.pshadowMaps[3], NULL, NULL, NULL, dstBox, NULL, NULL, 0);
	}

	if (r_dynamicGlow->integer != 0)
	{
		// Composite the glow/bloom texture
		int blendFunc = 0;
		vec4_t color = { 1.0f, 1.0f, 1.0f, 1.0f };

		if ( r_dynamicGlow->integer == 2 )
		{
			// Debug output
			blendFunc = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO;
		}
		else if ( r_dynamicGlowSoft->integer )
		{
			blendFunc = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE_MINUS_SRC_COLOR;
			color[0] = color[1] = color[2] = r_dynamicGlowIntensity->value;
		}
		else
		{
			blendFunc = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;
			color[0] = color[1] = color[2] = r_dynamicGlowIntensity->value;
		}

		FBO_BlitFromTexture (tr.glowFboScaled[0]->colorImage[0], NULL, NULL, NULL, NULL, NULL, color, blendFunc);
	}

	qglBindFramebuffer(GL_FRAMEBUFFER, 0);
	glState.currentFBO = NULL;

	backEnd.framePostProcessed = qtrue;

	return (const void *)(cmd + 1);
}

static const void *RB_BeginTimedBlock( const void *data )
{
	const beginTimedBlockCommand_t *cmd = (const beginTimedBlockCommand_t *)data;
	if ( glRefConfig.timerQuery )
	{
		gpuFrame_t *currentFrame = &backEndData->frames[backEndData->realFrameNumber % MAX_FRAMES];
		gpuTimer_t *timer = currentFrame->timers + currentFrame->numTimers++;

		if ( cmd->timerHandle >= 0 && currentFrame->numTimers <= MAX_GPU_TIMERS )
		{
			gpuTimedBlock_t *timedBlock = currentFrame->timedBlocks + cmd->timerHandle;
			timedBlock->beginTimer = timer->queryName;
			timedBlock->name = cmd->name;

			currentFrame->numTimedBlocks++;

			qglQueryCounter( timer->queryName, GL_TIMESTAMP );
		}
	}

	return (const void *)(cmd + 1);
}

static const void *RB_EndTimedBlock( const void *data )
{
	const endTimedBlockCommand_t *cmd = (const endTimedBlockCommand_t *)data;
	if ( glRefConfig.timerQuery )
	{
		gpuFrame_t *currentFrame = &backEndData->frames[backEndData->realFrameNumber % MAX_FRAMES];
		gpuTimer_t *timer = currentFrame->timers + currentFrame->numTimers++;

		if ( cmd->timerHandle >= 0 && currentFrame->numTimers <= MAX_GPU_TIMERS )
		{
			gpuTimedBlock_t *timedBlock = currentFrame->timedBlocks + cmd->timerHandle;
			timedBlock->endTimer = timer->queryName;

			qglQueryCounter( timer->queryName, GL_TIMESTAMP );
		}
	}

	return (const void *)(cmd + 1);
}

/*
=============
RB_ExportCubemaps

=============
*/
const void *RB_ExportCubemaps(const void *data)
{
	const exportCubemapsCommand_t *cmd = (exportCubemapsCommand_t *)data;

	// finish any 2D drawing if needed
	if (tess.numIndexes)
		RB_EndSurface();

	if (!tr.world || tr.numCubemaps == 0)
	{
		// do nothing
		ri.Printf(PRINT_ALL, "Nothing to export!\n");
		return (const void *)(cmd + 1);
	}

	if (cmd)
	{
		FBO_t *oldFbo = glState.currentFBO;
		int sideSize = r_cubemapSize->integer * r_cubemapSize->integer * 4;
		byte *cubemapPixels = (byte *)R_Malloc(sideSize * 6, TAG_TEMP_WORKSPACE);
		int i, j;

		FBO_Bind(tr.renderCubeFbo);

		for (i = 0; i < tr.numCubemaps; i++)
		{
			char filename[MAX_QPATH];
			cubemap_t *cubemap = &tr.cubemaps[i];
			byte *p = cubemapPixels;

			for (j = 0; j < 6; j++)
			{
				//FBO_AttachImage(tr.renderCubeFbo, cubemap->image, GL_COLOR_ATTACHMENT0_EXT, j);
				qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + j, cubemap->image->texnum, 0);

				qglReadPixels(0, 0, r_cubemapSize->integer, r_cubemapSize->integer, GL_RGBA, GL_UNSIGNED_BYTE, p);
				p += sideSize;
			}

			if (cubemap->name[0])
			{
				Com_sprintf(filename, MAX_QPATH, "cubemaps/%s/%s.dds", tr.world->baseName, cubemap->name);
			}
			else
			{
				Com_sprintf(filename, MAX_QPATH, "cubemaps/%s/%03d.dds", tr.world->baseName, i);
			}

			R_SaveDDS(filename, cubemapPixels, r_cubemapSize->integer, r_cubemapSize->integer, 6);
			ri.Printf(PRINT_ALL, "Saved cubemap %d as %s\n", i, filename);
		}

		FBO_Bind(oldFbo);

		R_Free(cubemapPixels);
	}

	return (const void *)(cmd + 1);
}

/*
=============
RB_StartBuildingSphericalHarmonics

=============
*/
const void *RB_StartBuildingSphericalHarmonics(const void *data)
{
	const startBuildingSphericalHarmonicsCommand_t *cmd = (startBuildingSphericalHarmonicsCommand_t *)data;

	tr.numfinishedSphericalHarmonics = 0;
	tr.buildingSphericalHarmonics = qtrue;

	return (const void *)(cmd + 1);
}

/*
=============
RB_BuildSphericalHarmonics

=============
*/
const void *RB_BuildSphericalHarmonics(const void *data)
{
	const buildSphericalHarmonicsCommand_t *cmd = (buildSphericalHarmonicsCommand_t *)data;

	// finish any 2D drawing if needed
	if (tess.numIndexes)
		RB_EndSurface();

	if (!tr.world || tr.numSphericalHarmonics == 0 || !r_cubeMapping->integer)
	{
		// do nothing
		ri.Printf(PRINT_ALL, "No world or no cubemapping enabled!\n");
		return (const void *)(cmd + 1);
	}

	if (cmd)
	{
		const int shSize = 32;
		const int sideSize = shSize * shSize * 4;
		const int batchSize = 32;

		GLenum cubemapFormat = GL_RGBA8;

		if (r_hdr->integer)
		{
			cubemapFormat = GL_RGBA16F;
		}
		image_t *bufferImage = R_FindImageFile("*sphericalHarmonic_buffer_image", IMGTYPE_COLORALPHA, IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE | IMGFLAG_MIPMAP | IMGFLAG_CUBEMAP);
		if (!bufferImage)
			bufferImage = R_CreateImage("*sphericalHarmonic_buffer_image", NULL, shSize, shSize, 16, IMGTYPE_COLORALPHA, IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE | IMGFLAG_MIPMAP | IMGFLAG_CUBEMAP, cubemapFormat);
		cubemap_t *currentSH = (cubemap_t *)R_Malloc(sizeof(*tr.cubemaps), TAG_TEMP_WORKSPACE);
		currentSH[0].image = bufferImage;
		int buildedSphericalHarmonics = 0;

		float *cubemapPixels = (float *)R_Malloc(sideSize * sizeof(float), TAG_TEMP_WORKSPACE);

		for (int i = tr.numfinishedSphericalHarmonics; i < (tr.numfinishedSphericalHarmonics + batchSize); i++)
		{
			if (i == tr.numSphericalHarmonics)
				break;

			VectorCopy(tr.sphericalHarmonicsCoefficients[i].origin, currentSH[0].origin);
			
			for (int j = 0; j < 6; j++)
			{
				RE_ClearScene();
				R_RenderCubemapSide(currentSH, j, qfalse, qtrue);
				R_IssuePendingRenderCommands();
				R_InitNextFrame();
			}

			//Convolve the cubemaps & build SH Coefficients
			//paper: http://www.graphics.stanford.edu/papers/envmap/envmap.pdf
			cubemap_t *sh = currentSH;
			sphericalHarmonic_t *shC = &tr.sphericalHarmonicsCoefficients[i];

			int numSamples = 0;
			for (int j = 0; j < 6; j++)
			{
				//read pixels into byte buffer
				float *p = cubemapPixels;
				qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + j, sh[0].image->texnum, 0);
				qglReadPixels(0, 0, shSize, shSize, GL_RGBA, GL_FLOAT, p);

				//build coefficients for current face
				for (int u = 0; u < shSize; u++)
				{
					for (int v = 0; v < shSize; v++)
					{
						vec2_t uv;
						vec3_t colorSample;
						vec3_t normal;
						float shBasis[9];

						VectorSet2(uv, (float)u / (float)shSize, (float)v / (float)shSize);
						VectorSet(colorSample,
							sRGBtoRGB(p[0]),
							sRGBtoRGB(p[1]),
							sRGBtoRGB(p[2])
							);

						//TODO: weight with solid angle!
						GetTextureAngle(uv, j, normal);
						GetSHBasis(normal, shBasis);

						vec3_t sample;
						for (int k = 0; k < 9; k++)
						{
							VectorScale(colorSample, shBasis[k], sample);
							VectorAdd(shC->coefficents[k], sample, shC->coefficents[k]);
						}
						numSamples++;
						p += 4;
					}
				}
			}
			// scale spherical harmonics coefficients by number of samples
			for (int i = 0; i < 9; i++)
			{
				VectorScale(shC->coefficents[i], 1.0/(float)numSamples, shC->coefficents[i]);
			}
			buildedSphericalHarmonics++;
		}
		tr.numfinishedSphericalHarmonics += buildedSphericalHarmonics;
		//TODO: Export them somehow. json file?
		if (tr.numfinishedSphericalHarmonics == tr.numSphericalHarmonics)
		{
			ri.Printf(PRINT_ALL, "Finished building all spherical harmonics for this level.\n");
			tr.buildingSphericalHarmonics = qfalse;
		}
		else
			ri.Printf(PRINT_ALL, "Finished building %i of %i spherical harmonics for this level. (%3.2f%%)\n", 
				tr.numfinishedSphericalHarmonics, 
				tr.numSphericalHarmonics, 
				((float)tr.numfinishedSphericalHarmonics / (float)tr.numSphericalHarmonics) * 100.f);

		R_Free(cubemapPixels);
		R_Free(currentSH);
	}

	return (const void *)(cmd + 1);
}

/*
====================
RB_ExecuteRenderCommands
====================
*/
void RB_ExecuteRenderCommands( const void *data ) {
	int		t1, t2;

	t1 = ri.Milliseconds ();

	while ( 1 ) {
		data = PADP(data, sizeof(void *));

		switch ( *(const int *)data ) {
		case RC_SET_COLOR:
			data = RB_SetColor( data );
			break;
		case RC_STRETCH_PIC:
			data = RB_StretchPic( data );
			break;
		case RC_ROTATE_PIC:
			data = RB_RotatePic( data );
			break;
		case RC_ROTATE_PIC2:
			data = RB_RotatePic2( data );
			break;
		case RC_ROTATE_PIC2_RATIOFIX:
			data = RB_RotatePic2RatioFix( data );
			break;
		case RC_SCISSOR:
			data = RB_Scissor(data);
			break;
		case RC_DRAW_SURFS:
			data = RB_DrawSurfs( data );
			break;
		case RC_DRAW_BUFFER:
			data = RB_DrawBuffer( data );
			break;
		case RC_SWAP_BUFFERS:
			data = RB_SwapBuffers( data );
			break;
		case RC_SCREENSHOT:
			data = RB_TakeScreenshotCmd( data );
			break;
		case RC_COLORMASK:
			data = RB_ColorMask(data);
			break;
		case RC_CLEARDEPTH:
			data = RB_ClearDepth(data);
			break;
		case RC_CONVOLVECUBEMAP:
			data = RB_PrefilterEnvMap(data);
			break;
		case RC_PROJECTCUBEMAP:
			data = RB_ProjectCubeMap(data);
			break;
		case RC_POSTPROCESS:
			data = RB_PostProcess(data);
			break;
		case RC_EXPORT_CUBEMAPS:
			data = RB_ExportCubemaps(data);
			break;
		case RC_BUILD_SPHERICAL_HARMONICS:
			data = RB_BuildSphericalHarmonics(data);
			break;
		case RC_START_BUILDING_SPHERICAL_HARMONICS:
			data = RB_StartBuildingSphericalHarmonics(data);
			break;
		case RC_BEGIN_TIMED_BLOCK:
			data = RB_BeginTimedBlock(data);
			break;
		case RC_END_TIMED_BLOCK:
			data = RB_EndTimedBlock(data);
			break;
		case RC_END_OF_LIST:
		default:
			// finish any 2D drawing if needed
			if(tess.numIndexes)
				RB_EndSurface();

			// stop rendering
			t2 = ri.Milliseconds ();
			backEnd.pc.msec = t2 - t1;
			return;
		}
	}

}
