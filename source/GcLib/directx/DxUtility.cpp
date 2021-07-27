#include "source/GcLib/pch.h"

#include "DxUtility.hpp"

using namespace gstd;
using namespace directx;

#if defined(DNH_PROJ_EXECUTOR)

//*******************************************************************
//ColorAccess
//*******************************************************************
D3DCOLORVALUE ColorAccess::MultiplyColor(D3DCOLORVALUE& value, D3DCOLOR color) {
	D3DXVECTOR4 _col = ToVec4Normalized(color);							//argb
	D3DXVECTOR4 colVec = D3DXVECTOR4(_col.y, _col.z, _col.w, _col.x);	//rgba
	value = (D3DCOLORVALUE&)MultiplyColor(colVec, color);
	return value;
}
D3DMATERIAL9 ColorAccess::MultiplyColor(D3DMATERIAL9 mat, D3DCOLOR color) {
	D3DXVECTOR4 _col = ToVec4Normalized(color);					//argb
	__m128 col = Vectorize::Set(_col.y, _col.z, _col.w, _col.x);	//rgba
	__m128 dst;

	auto PerformMul = [&](D3DCOLORVALUE* src) {
		memcpy(&dst, src, sizeof(D3DCOLORVALUE));	//rgba
		dst = Vectorize::Mul(Vectorize::Load((float*)src), col);
		Vectorize::Store((float*)src, dst);
	};
	PerformMul(&mat.Diffuse);
	PerformMul(&mat.Specular);
	PerformMul(&mat.Ambient);
	PerformMul(&mat.Emissive);

	return mat;
}
D3DXVECTOR4& ColorAccess::MultiplyColor(D3DXVECTOR4& value, D3DCOLOR color) {
	__m128 v1 = Vectorize::Load(&value.x);					//argb
	__m128 col = Vectorize::Load(ToVec4Normalized(color));	//argb
	v1 = Vectorize::Mul(v1, col);
	value = ClampColorPacked((__m128i&)v1);
	return value;
}
D3DCOLOR& ColorAccess::MultiplyColor(D3DCOLOR& src, const D3DCOLOR& mul) {
	D3DXVECTOR4 mulFac = ToVec4Normalized(mul);
	__m128 vsrc = Vectorize::Load(ToVec4(src));			//argb
	__m128 res = Vectorize::Mul(vsrc, Vectorize::Load(mulFac));
	__m128i argb = ClampColorPackedM((D3DXVECTOR4&)res);
	src = ToD3DCOLOR(argb);
	return src;
}
D3DCOLOR& ColorAccess::ApplyAlpha(D3DCOLOR& color, float alpha) {
	__m128 v1 = Vectorize::Load(ToVec4(color));			//argb
	__m128 v2 = Vectorize::Replicate(alpha);
	v1 = Vectorize::Mul(v1, v2);
	__m128i ci = ClampColorPackedM((D3DXVECTOR4&)v1);
	color = ToD3DCOLOR(ci);
	return color;
}

D3DXVECTOR4 ColorAccess::ClampColorPacked(const D3DXVECTOR4& src) {
	__m128i ci = Vectorize::SetI(src[0], src[1], src[2], src[3]);
	return ColorAccess::ClampColorPacked(ci);
}
D3DXVECTOR4 ColorAccess::ClampColorPacked(const __m128i& src) {
	__m128i ci = ColorAccess::ClampColorPackedM(src);
	return D3DXVECTOR4(ci.m128i_i32[0], ci.m128i_i32[1], ci.m128i_i32[2], ci.m128i_i32[3]);
}
__m128i ColorAccess::ClampColorPackedM(const D3DXVECTOR4& src) {
	__m128i ci = Vectorize::SetI(src[0], src[1], src[2], src[3]);
	return ColorAccess::ClampColorPackedM(ci);
}
__m128i ColorAccess::ClampColorPackedM(const __m128i& src) {
	return (__m128i&)Vectorize::ClampPacked(src, 
		Vectorize::Replicate(0x00), Vectorize::Replicate(0xff));
}

D3DXVECTOR3& ColorAccess::RGBtoHSV(D3DXVECTOR3& color, int red, int green, int blue) {
	//[In]  RGB: (0 ~ 255, 0 ~ 255, 0 ~ 255)
	//[Out] HSV: (0 ~ 360, 0 ~ 255, 0 ~ 255)

	int cmax = std::max(std::max(red, green), blue);
	int cmin = std::min(std::min(red, green), blue);
	float delta = cmax - cmin;

	color.z = cmax;
	if (delta < 0.00001f) {
		color.x = 0;
		color.y = 0;
	}
	else if (cmax == 0) {
		color.x = NAN;
		color.y = 0;
	}
	else {
		color.y = (int)std::roundf(delta / cmax * 255.0f);

		if (red >= cmax)
			color.x = (green - blue) / delta;
		else if (green >= cmax)
			color.x = 2.0f + (blue - red) / delta;
		else
			color.x = 4.0f + (red - green) / delta;

		color.x = (int)Math::NormalizeAngleDeg(color.x * 60.0f);
	}

	return color;
}
D3DCOLOR& ColorAccess::HSVtoRGB(D3DCOLOR& color, int hue, int saturation, int value) {
	//Hue: 0 ~ 360
	//Sat: 0 ~ 255
	//Val: 0 ~ 255

	int hh = hue % 360;
	if (hh < 0) hh += 360;

	int i = hh / 60;
	float ff = (hh % 60) / 60.0f;
	float s = saturation / 255.0f;

	int p = value * (1.0f - s);
	int q = value * (1.0f - s * ff);
	int t = value * (1.0f - s * (1.0f - ff));

	auto GenColor = [](int r, int g, int b) -> D3DCOLOR {
		__m128i ci = Vectorize::Set(r, g, b, 0);
		ci = ColorAccess::ClampColorPackedM(ci);
		return D3DCOLOR_XRGB(ci.m128i_i32[0], ci.m128i_i32[1], ci.m128i_i32[2]);
	};

	D3DCOLOR rgb = 0xffffffff;
	switch (i) {
	case 0: rgb = GenColor(value, t, p); break;
	case 1: rgb = GenColor(q, value, p); break;
	case 2: rgb = GenColor(p, value, t); break;
	case 3: rgb = GenColor(p, q, value); break;
	case 4: rgb = GenColor(t, p, value); break;
	case 5: 
	default:
			rgb = GenColor(value, p, q); break;
	}

	color = (rgb & 0x00ffffff) | (color & 0xff000000);
	return color;
}

//Permute format
//MSB        LSB   (8 bits)
//00  00  00  00
//1st 2nd 3rd 4th
D3DXVECTOR4 ColorAccess::ToVec4(const D3DCOLOR& color, uint8_t permute) {
	byte lColor[4] = { GetColorA(color), GetColorR(color), GetColorG(color), GetColorB(color) };
	return D3DXVECTOR4(lColor[(permute >> 6) & 3], lColor[(permute >> 4) & 3], 
		lColor[(permute >> 2) & 3], lColor[permute & 3]);
}
D3DXVECTOR4 ColorAccess::ToVec4Normalized(const D3DCOLOR& color, uint8_t permute) {
	__m128 argb = Vectorize::Load(ToVec4(color, permute));
	__m128 nor = Vectorize::Replicate(1.0f / 255.0f);
	nor = Vectorize::Mul(argb, nor);
	return (D3DXVECTOR4&)nor;
}

//*******************************************************************
//DxMath
//*******************************************************************
void DxMath::ConstructRotationMatrix(D3DXMATRIX* mat, const D3DXVECTOR2& angleX, 
	const D3DXVECTOR2& angleY, const D3DXVECTOR2& angleZ)
{
	float cx = angleX.x;
	float sx = angleX.y;
	float cy = angleY.x;
	float sy = angleY.y;
	float cz = angleZ.x;
	float sz = angleZ.y;
	float sx_sy = sx * sy;
	float sx_cy = sx * cy;
	//I am unable to make a vectorized code that performs better than the unvectorized code
	/*
	__m128 v1 = Vectorize::Mul(Vectorize::Set(cy, sx_sy, cx, sy), Vectorize::Set(cz, sz, sz, cz));
	__m128 v2 = Vectorize::Mul(Vectorize::Set(sx_cy, cy, sx_sy, cx), Vectorize::Set(sz, sz, cz, cz));
	__m128 v3 = Vectorize::Mul(Vectorize::Set(sy, sx_cy, cx, cx), Vectorize::Set(sz, cz, sy, cy));

	mat->_12 = -v1.m128_f32[2];
	mat->_22 = v2.m128_f32[3];
	mat->_31 = -v3.m128_f32[2];
	mat->_32 = sx;
	mat->_33 = v3.m128_f32[3];

	v1 = Vectorize::AddSub(
		Vectorize::Set(v1.m128_f32[0], v1.m128_f32[3], v3.m128_f32[0], v2.m128_f32[1]),
		Vectorize::Set(v1.m128_f32[1], v2.m128_f32[0], v3.m128_f32[1], v2.m128_f32[2]));
	mat->_11 = v1.m128_f32[0];
	mat->_13 = v1.m128_f32[1];
	mat->_21 = v1.m128_f32[3];
	mat->_23 = v1.m128_f32[2];
	*/
	mat->_11 = cy * cz - sx_sy * sz;
	mat->_12 = -cx * sz;
	mat->_13 = sy * cz + sx_cy * sz;
	mat->_21 = cy * sz + sx_sy * cz;
	mat->_22 = cx * cz;
	mat->_23 = sy * sz - sx_cy * cz;
	mat->_31 = -cx * sy;
	mat->_32 = sx;
	mat->_33 = cx * cy;
}
void DxMath::MatrixApplyScaling(D3DXMATRIX* mat, const D3DXVECTOR3& scale) {
	//Vectorized / Unvectorized -> ~0.46 (x2.17 times)
#ifdef __L_MATH_VECTORIZE
	__m128 v_mat = Vectorize::Mul(
		Vectorize::Set(mat->_11, mat->_12, mat->_13, mat->_21),
		Vectorize::Set(scale.x, scale.x, scale.x, scale.y));
	mat->_11 = v_mat.m128_f32[0];
	mat->_12 = v_mat.m128_f32[1];
	mat->_13 = v_mat.m128_f32[2];
	mat->_21 = v_mat.m128_f32[3];

	v_mat = Vectorize::Mul(
		Vectorize::Set(mat->_22, mat->_23, mat->_31, mat->_32),
		Vectorize::Set(scale.y, scale.y, scale.z, scale.z));
	mat->_22 = v_mat.m128_f32[0];
	mat->_23 = v_mat.m128_f32[1];
	mat->_31 = v_mat.m128_f32[2];
	mat->_32 = v_mat.m128_f32[3];

	mat->_33 = mat->_33 * scale.z;
#else
	mat->_11 *= scale.x;
	mat->_12 *= scale.x;
	mat->_13 *= scale.x;
	mat->_21 *= scale.y;
	mat->_22 *= scale.y;
	mat->_23 *= scale.y;
	mat->_31 *= scale.z;
	mat->_32 *= scale.z;
	mat->_33 *= scale.z;
#endif
}
D3DXVECTOR4 DxMath::RotatePosFromXYZFactor(D3DXVECTOR4& vec, D3DXVECTOR2* angX, D3DXVECTOR2* angY, D3DXVECTOR2* angZ) {
	float vx = vec.x;
	float vy = vec.y;
	float vz = vec.z;
	float cx = angX->x;
	float sx = angX->y;
	float cy = angY->x;
	float sy = angY->y;
	float cz = angZ->x;
	float sz = angZ->y;
#ifdef __L_MATH_VECTORIZE
	if (angZ) {
		__m128 v_res = Vectorize::Mul(Vectorize::Set(vx, vy, vx, vy), Vectorize::Set(cz, sz, sz, cz));
		vec.x = v_res.m128_f32[0] - v_res.m128_f32[1];
		vec.y = v_res.m128_f32[2] + v_res.m128_f32[3];
		vx = vec.x;
		vy = vec.y;
	}
	if (angX) {
		__m128 v_res = Vectorize::Mul(Vectorize::Set(vy, vz, vy, vz), Vectorize::Set(cx, sx, sx, cx));
		vec.y = v_res.m128_f32[0] - v_res.m128_f32[1];
		vec.z = v_res.m128_f32[2] + v_res.m128_f32[3];
		vy = vec.y;
		vz = vec.z;
	}
	if (angY) {
		__m128 v_res = Vectorize::Mul(Vectorize::Set(vz, vx, vz, vx), Vectorize::Set(sy, cy, cy, sy));
		vec.x = v_res.m128_f32[0] + v_res.m128_f32[1];
		vec.z = v_res.m128_f32[2] - v_res.m128_f32[3];
	}
#else
	if (angZ) {
		vec.x = vx * cz - vy * sz;
		vec.y = vx * sz + vy * cz;
		vx = vec.x;
		vy = vec.y;
	}
	if (angX) {
		vec.y = vy * cx - vz * sx;
		vec.z = vy * sx + vz * cx;
		vy = vec.y;
		vz = vec.z;
	}
	if (angY) {
		vec.x = vz * sy + vx * cy;
		vec.z = vz * cy - vx * sy;
	}
#endif
	return vec;
}
void DxMath::TransformVertex2D(VERTEX_TLX(&vert)[4], D3DXVECTOR2* scale, D3DXVECTOR2* angle,
	D3DXVECTOR2* position, D3DXVECTOR2* textureSize) 
{
	//Vectorized / Unvectorized -> ~0.35 (x2.86 times)
#ifdef __L_MATH_VECTORIZE
	__m128 v1, v2, v3;

	//Divide the UVs, textureSize should already be inverted
	{
		v3 = Vectorize::Set(textureSize->x, textureSize->y, textureSize->x, textureSize->y);
		for (size_t i = 0; i < 4; i += 2) {		//2 vertices at a time
			v1 = Vectorize::Mul(
				Vectorize::Set(vert[i].texcoord.x, vert[i].texcoord.y, vert[i + 1].texcoord.x, vert[i + 1].texcoord.y), v3);
			memcpy(&(vert[i + 0].texcoord), &v1.m128_f32[0], sizeof(D3DXVECTOR2));
			memcpy(&(vert[i + 1].texcoord), &v1.m128_f32[2], sizeof(D3DXVECTOR2));
		}
	}

	//Initialize rotation factor and then scale
	v3 = Vectorize::Mul(
		Vectorize::Set(angle->x, angle->y, angle->y, angle->x),
		Vectorize::Set(scale->x, scale->y, scale->x, scale->y));
	for (size_t i = 0; i < 4; i += 2) {			//2 vertices at a time
		//Rotate
		v1 = Vectorize::Mul(Vectorize::Set(vert[i + 0].position.x, vert[i + 0].position.y,
			vert[i + 0].position.x, vert[i + 0].position.y), v3);
		v2 = Vectorize::Mul(Vectorize::Set(vert[i + 1].position.x, vert[i + 1].position.y,
			vert[i + 1].position.x, vert[i + 1].position.y), v3);
		v1 = Vectorize::AddSub(
			Vectorize::Set(v1.m128_f32[0], v1.m128_f32[2], v2.m128_f32[0], v2.m128_f32[2]), 
			Vectorize::Set(v1.m128_f32[1], v1.m128_f32[3], v2.m128_f32[1], v2.m128_f32[3]));

		//Translate
		v2 = Vectorize::Add(v1, 
			Vectorize::Set(position->x, position->y, position->x, position->y));

		memcpy(&(vert[i + 0].position), &v2.m128_f32[0], sizeof(D3DXVECTOR2));
		memcpy(&(vert[i + 1].position), &v2.m128_f32[2], sizeof(D3DXVECTOR2));
	}
#else
	vert[0].texcoord.x *= textureSize->x;
	vert[0].texcoord.y *= textureSize->y;
	vert[1].texcoord.x *= textureSize->x;
	vert[1].texcoord.y *= textureSize->y;
	vert[2].texcoord.x *= textureSize->x;
	vert[2].texcoord.y *= textureSize->y;
	vert[3].texcoord.x *= textureSize->x;
	vert[3].texcoord.y *= textureSize->y;

	for (size_t i = 0; i < 4; ++i) {
		D3DXVECTOR4& vPos = vert[i].position;
		float px = vPos.x;
		float py = vPos.y;
		vPos.x = fmaf(px * angle->x - py * angle->y, scale->x, position->x);
		vPos.y = fmaf(px * angle->y + py * angle->x, scale->y, position->y);
	}
#endif
}

#endif