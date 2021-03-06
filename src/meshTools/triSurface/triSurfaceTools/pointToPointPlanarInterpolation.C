/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2012-2013 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

#include "pointToPointPlanarInterpolation.H"
#include "boundBox.H"
#include "Random.H"
#include "vector2D.H"
#include "triSurface.H"
#include "triSurfaceTools.H"
#include "OFstream.H"
#include "Time.H"

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{
defineTypeNameAndDebug(pointToPointPlanarInterpolation, 0);
}


// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

Foam::coordinateSystem
Foam::pointToPointPlanarInterpolation::calcCoordinateSystem
(
    const pointField& points
) const
{
    if (points.size() < 3)
    {
        FatalErrorIn
        (
            "pointToPointPlanarInterpolation::calcCoordinateSystem"
            "(const pointField&)"
        )   << "Only " << points.size() << " provided." << nl
            << "Need at least three non-colinear points"
            << " to be able to interpolate."
            << exit(FatalError);
    }

    const point& p0 = points[0];

    // Find furthest away point
    vector e1;
    label index1 = -1;
    scalar maxDist = -GREAT;

    for (label i = 1; i < points.size(); i++)
    {
        const vector d = points[i] - p0;
        scalar magD = mag(d);

        if (magD > maxDist)
        {
            e1 = d/magD;
            index1 = i;
            maxDist = magD;
        }
    }
    // Find point that is furthest away from line p0-p1
    const point& p1 = points[index1];

    label index2 = -1;
    maxDist = -GREAT;
    for (label i = 1; i < points.size(); i++)
    {
        if (i != index1)
        {
            const point& p2 = points[i];
            vector e2(p2 - p0);
            e2 -= (e2&e1)*e1;
            scalar magE2 = mag(e2);

            if (magE2 > maxDist)
            {
                index2 = i;
                maxDist = magE2;
            }
        }
    }
    if (index2 == -1)
    {
        FatalErrorIn
        (
            "pointToPointPlanarInterpolation::calcCoordinateSystem"
            "(const pointField&)"
        )   << "Cannot find points that make valid normal." << nl
            << "Have so far points " << p0 << " and " << p1
            << "Need at least three points which are not in a line."
            << exit(FatalError);
    }

    vector n = e1^(points[index2]-p0);
    n /= mag(n);

    if (debug)
    {
        Info<< "pointToPointPlanarInterpolation::calcCoordinateSystem :"
            << " Used points " << p0 << ' ' << points[index1]
            << ' ' << points[index2]
            << " to define coordinate system with normal " << n << endl;
    }

    return coordinateSystem
    (
        "reference",
        p0,  // origin
        n,   // normal
        e1   // 0-axis
    );
}


void Foam::pointToPointPlanarInterpolation::calcWeights
(
    const pointField& sourcePoints,
    const pointField& destPoints
)
{
    tmp<vectorField> tlocalVertices
    (
        referenceCS_.localPosition(sourcePoints)
    );
    vectorField& localVertices = tlocalVertices();

    const boundBox bb(localVertices, true);
    const point bbMid(bb.midpoint());

    if (debug)
    {
        Info<< "pointToPointPlanarInterpolation::readData :"
            << " Perturbing points with " << perturb_
            << " fraction of a random position inside " << bb
            << " to break any ties on regular meshes."
            << nl << endl;
    }

    Random rndGen(123456);
    forAll(localVertices, i)
    {
        localVertices[i] +=
            perturb_
           *(rndGen.position(bb.min(), bb.max())-bbMid);
    }

    // Determine triangulation
    List<vector2D> localVertices2D(localVertices.size());
    forAll(localVertices, i)
    {
        localVertices2D[i][0] = localVertices[i][0];
        localVertices2D[i][1] = localVertices[i][1];
    }

    triSurface s(triSurfaceTools::delaunay2D(localVertices2D));

    tmp<pointField> tlocalFaceCentres
    (
        referenceCS_.localPosition
        (
            destPoints
        )
    );
    const pointField& localFaceCentres = tlocalFaceCentres();

    if (debug)
    {
        Pout<< "pointToPointPlanarInterpolation::readData :"
            <<" Dumping triangulated surface to triangulation.stl" << endl;
        s.write("triangulation.stl");

        OFstream str("localFaceCentres.obj");
        Pout<< "readSamplePoints :"
            << " Dumping face centres to " << str.name() << endl;

        forAll(localFaceCentres, i)
        {
            const point& p = localFaceCentres[i];
            str<< "v " << p.x() << ' ' << p.y() << ' ' << p.z() << nl;
        }
    }

    // Determine interpolation onto face centres.
    triSurfaceTools::calcInterpolationWeights
    (
        s,
        localFaceCentres,   // points to interpolate to
        nearestVertex_,
        nearestVertexWeight_
    );
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::pointToPointPlanarInterpolation::pointToPointPlanarInterpolation
(
    const pointField& sourcePoints,
    const pointField& destPoints,
    const scalar perturb
)
:
    perturb_(perturb),
    referenceCS_(calcCoordinateSystem(sourcePoints)),
    nPoints_(sourcePoints.size())

{
    calcWeights(sourcePoints, destPoints);
}


Foam::pointToPointPlanarInterpolation::pointToPointPlanarInterpolation
(
    const coordinateSystem& referenceCS,
    const pointField& sourcePoints,
    const pointField& destPoints,
    const scalar perturb
)
:
    perturb_(perturb),
    referenceCS_(referenceCS),
    nPoints_(sourcePoints.size())
{
    calcWeights(sourcePoints, destPoints);
}


// * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * * //

Foam::wordList Foam::pointToPointPlanarInterpolation::timeNames
(
    const instantList& times
)
{
    wordList names(times.size());

    forAll(times, i)
    {
        names[i] = times[i].name();
    }
    return names;
}


bool Foam::pointToPointPlanarInterpolation::findTime
(
    const instantList& times,
    const label startSampleTime,
    const scalar timeVal,
    label& lo,
    label& hi
)
{
    lo = startSampleTime;
    hi = -1;

    for (label i = startSampleTime+1; i < times.size(); i++)
    {
        if (times[i].value() > timeVal)
        {
            break;
        }
        else
        {
            lo = i;
        }
    }

    if (lo == -1)
    {
        //FatalErrorIn("findTime(..)")
        //    << "Cannot find starting sampling values for current time "
        //    << timeVal << nl
        //    << "Have sampling values for times "
        //    << timeNames(times) << nl
        //    << exit(FatalError);
        return false;
    }

    if (lo < times.size()-1)
    {
        hi = lo+1;
    }


    if (debug)
    {
        if (hi == -1)
        {
            Pout<< "findTime : Found time " << timeVal << " after"
                << " index:" << lo << " time:" << times[lo].value()
                << endl;
        }
        else
        {
            Pout<< "findTime : Found time " << timeVal << " inbetween"
                << " index:" << lo << " time:" << times[lo].value()
                << " and index:" << hi << " time:" << times[hi].value()
                << endl;
        }
    }
    return true;
}


// ************************************************************************* //
