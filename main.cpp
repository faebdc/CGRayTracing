#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <vector>

using namespace std;

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "headers/vec3.h"
#include "headers/util.h"
#include "headers/objects.h"
#include "headers/sampling.h"
#include "headers/hitpoints.h"
#include "headers/hash.h"

const double eps = 1e-4;
const double INF = 1e10;
const double PI = 3.14159265358979;

const int width = 1024;
const int height = 768;
// image_data : the data for generating PNG
char image_data[width * height * 3];
// image : the image data in the computation
Vec3 image[height][width];

const int MAX_DEPTH = 5;
const double alpha = 0.7;

const Vec3 background = Vec3(0.0, 0.0, 0.0);

int debug_counter = 0;

void trace(const Vec3 &org, const Vec3 &dir, const vector<Object *> &objs, Vec3 flux, Vec3 adj, bool flag, int depth, Hashtable & htable, int x, int y) {
	// org is the origin of the ray
	// dir is the direction of the ray, should be normalized
	// depth is the current depth of the ray tracing algorithm
	if (depth >= MAX_DEPTH) {
		return;
	}
	// find the nearest intersection point
	double len;
	int id = -1;
	double nearest = INF;
	for (int i = 0; i < objs.size(); i++) {
		if (objs[i]->intersect(org, dir, len)) {
			if (len < nearest) {
				id = i;
				nearest = len;
			}
		}
	}
	if (id == -1) { // no intersection with the objects
		return;
	}
	const Object * obj = objs[id];
	Vec3 intersection = org + dir * nearest;
	Vec3 normalvec = obj->normalvec(intersection);
	// inside the object, change the direction of the normal vector
	if (normalvec.dot(dir) > 0) {
		normalvec = -normalvec;
	}
	Vec3 f = obj->getSurfaceColor();

	double p = max(f.x, f.y, f.z);


	if (obj->getReflection() < eps && obj->getTransparency() < eps) {
		// diffusion
		double r = 400.0 / height;
		if (flag) {
			// the ray from the eye
			Hitpoint hp = Hitpoint();
			hp.f = f * adj;
			hp.pos = intersection;
			hp.normal = normalvec;
			hp.w = x;
			hp.h = y;
			hp.flux = Vec3();
			hp.r2 = r * r;
			hp.n = 0;
			//hitpoints.push_back(hp);
			//printf("begin insert\n");
			htable.insert(hp);
			//printf("end insert\n");
		}
		else {
			// the photon ray from the light source
			int ix, iy, iz;
			htable.compute_coord(intersection.x, intersection.y, intersection.z, ix, iy, iz);
			ix -= 1;
			iy -= 1;
			iz -= 1;
			int idx = 0, idy = 0, idz = 0;
			// search for the adjacent grids
			for (idx = 0; idx < 3; idx++)
			for (idy = 0; idy < 3; idy++)
			for (idz = 0; idz < 3; idz++) {
				int hashid = htable.hash(ix + idx, iy + idy, iz + idz);
			for (int i = 0; i < htable.hashtable[hashid].size(); i++) {
				Vec3 d = htable.hashtable[hashid][i].pos - intersection;
				if ((htable.hashtable[hashid][i].normal.dot(normalvec) > eps) && (d.dot(d) <= htable.hashtable[hashid][i].r2)) {
					// the equations here comes from the equations in smallppm.cpp
					// in smallppm.cpp, n = N / alpha
					double g = (htable.hashtable[hashid][i].n * alpha + alpha) / (htable.hashtable[hashid][i].n * alpha + 1.0);
					htable.hashtable[hashid][i].r2 *= g;
					htable.hashtable[hashid][i].n++;
					htable.hashtable[hashid][i].flux = (htable.hashtable[hashid][i].flux + htable.hashtable[hashid][i].f.mul(flux) * (1.0 / PI)) * g;
				}
			}
			}
			Vec3 newdir = uniform_sampling_halfsphere(normalvec);
			trace(intersection, newdir, objs, f * flux * (1.0 / p), adj, flag, depth+1, htable, x, y);
		}
	}
}

void render(const vector<Object *> &objs) {
	// the axis: x, left to right(width)
	//           y, bottom to top(height)
	//           z, satisfies the right hand rule
	// the cam should be at (0,0,-10)
	// the image has z = 0
	// the x axis of the image range from (-10,10)
	// in this project, we assume that there is only 1 point light source
	// the light source locate that (0,50,20)
	Vec3 lightorg = Vec3(0,25,20);
	Vec3 camorg = Vec3(0,0,-10);
	//vector<Hitpoint> hitpoints;
	double r = 400.0 / height;
	Hashtable htable = Hashtable(100001,r);
	for (int h = 0; h < height; h++) {
		fprintf(stderr, "\rHitPointPass %5.2f%%", 100.0 * (h+1) / height);
		for (int w = 0; w < width; w++) {
			double x = (2.0 * ((double)w/width)-1) * 10.0;
			double y = (2.0 * ((double)h/height)-1) * 10.0 * height / width;
			Vec3 dir = (Vec3(x,y,0) - camorg).normalize();
			trace(camorg, dir, objs, Vec3(), Vec3(1,1,1), true, 0, htable, w, h);
		}
	}
	fprintf(stderr,"\n"); 
	int num_photon = 100000;

	//#pragma omp parallel for schedule(dynamic, 1)
	for (int i = 0; i < num_photon; i++) {
		double p = 100.0 * (i+1) / num_photon;
		fprintf(stderr, "\rPhotonPass %5.2f%%",p);
		Vec3 dir = uniform_sampling_sphere();
		trace(lightorg, dir, objs, Vec3(2500,2500,2500)*(PI*4.0), Vec3(1,1,1), false, 0, htable, 0, 0);
	}

	//vector<Hitpoint> * ptr = htable.getPtr();
	for (int j = 0; j < htable.hashtable.size(); j++) {
		//vector<Hitpoint> * nptr = ptr + j;
		for (int i = 0; i < htable.hashtable[j].size(); i++) {
			Hitpoint hp = htable.hashtable[j][i];
			image[hp.h][hp.w] = image[hp.h][hp.w] + hp.flux * (1.0/(PI*hp.r2*num_photon));
		}
	}

	int debug_counter = 0;
	for (int i = 0; i < htable.hashtable.size(); i++) {
		debug_counter += htable.hashtable[i].size();
	}

	printf("\nhitpoints: %d\n", debug_counter);
}

int main(int argc, char *argv[]) {
	srand(19);
	//initialize the image data
	for (int i = 0; i < height; i++) {
		for (int j = 0; j < width; j++) {
			image[i][j] = Vec3(0,0,0);
		}
	}

	vector<Object *> objs;
	vector<Sphere> sphs;
	sphs.push_back(Sphere(Vec3(0.0, -10020, 0.0), 10000, Vec3(0.75, 0.25, 0.25), 0.0, 0.0));
	sphs.push_back(Sphere(Vec3(10030, 0.0, 0.0), 10000, Vec3(0.25, 0.25, 0.75), 0.0, 0.0));
	sphs.push_back(Sphere(Vec3(-10030, 0.0, 0.0), 10000, Vec3(0.75, 0.75, 0.75), 0.0, 0.0));
	sphs.push_back(Sphere(Vec3(0.0, 0.0, 10040), 10000, Vec3(0.57, 0.75, 0.75), 0.0, 0.0));
	sphs.push_back(Sphere(Vec3(0.0, 10040, 0.0), 10000, Vec3(0.5, 0.5, 0.5), 0.0, 0.0));
	sphs.push_back(Sphere(Vec3(0.0, 0.0, -10020), 10000, Vec3(0.75, 0.75, 0.75), 0.0, 0.0));
	sphs.push_back(Sphere(Vec3(-10.0, 0.0, 30), 10, Vec3(0.3, 0.3, 0.3), 0.0, 0.0));
	

	Object * obj;
	for (int i = 0; i < sphs.size(); i++) {
		obj = &sphs[i];
		objs.push_back(obj);
	}

	// render
	render(objs);
	// generate PNG
	int counter = 0;
	for (int i = 0; i < height; i++) {
		for (int j = 0; j < width; j++) {
			image_data[3 * counter] = gammaCorr(image[height - i][j].x);
        	image_data[3 * counter + 1] = gammaCorr(image[height - i][j].y);
        	image_data[3 * counter + 2] = gammaCorr(image[height - i][j].z);
			counter++;
		}
	}
    stbi_write_png("test.png", width, height, 3, image_data, width * 3);
    return 0;
}
