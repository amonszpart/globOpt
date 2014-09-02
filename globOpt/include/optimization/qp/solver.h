#ifndef __GF2_SOLVER_H__
#define __GF2_SOLVER_H__

//////////////
/// Solver
//////////////

#include "optimization/qp/gurobiOpt.h"
#include "globfit2/primitives/pointPrimitive.h"
#include "qcqpcpp/io/io.h"                      // read/writeSparseMatrix

namespace GF2 {

struct SolverParams
{
        int n_points = 50;
}; // ... struct SolverParams

class Solver
{
    public:
        typedef float                                       Scalar;
        typedef Eigen::Matrix<Scalar,3,1>                   Vector;
        typedef LinePrimitive2                              PrimitiveT;
        typedef PointPrimitive                              PointT;
        typedef std::vector<std::vector<PrimitiveT> >       PrimitiveContainerT;
        typedef std::vector<PointT>                         PointContainerT;
        typedef Eigen::SparseMatrix<Scalar,Eigen::RowMajor> SparseMatrix;

        //static inline int show       ( int argc, char** argv );
        static inline int sampleInput( int argc, char** argv );
        static inline int generate   ( int argc, char** argv );
        //static inline int formulate  ( int argc, char** argv );
        static inline int solve      ( int argc, char** argv );
        static inline int datafit    ( int argc, char** argv );

        //static inline int run        ( std::string img_path, Scalar const scale, std::vector<Scalar> const& angles, int argc, char** argv ) __attribute__ ((deprecated));

        static inline Eigen::Matrix<Scalar,3,1> checkSolution( std::vector<Scalar>       const& x
                                                             , SparseMatrix              const& qo
                                                             , SparseMatrix              const& Qo
                                                             , SparseMatrix              const& A
                                                             , Eigen::Matrix<Scalar,3,1> const& weights );
}; // ... cls Solver
} // ... ns gf2


//__________________________________HPP__________________________________________________

#include "Eigen/Sparse"

#ifdef GF2_USE_PCL
//#   include <pcl/visualization/pcl_visualizer.h>
#   include <pcl/console/parse.h>
#endif // GF2_USE_PCL

#ifdef GF2_WITH_MOSEK
#   include "qcqpcpp/mosekOptProblem.h"
#endif // GF2_WITH_MOSEK
#ifdef GF2_WITH_BONMIN
#   include "qcqpcpp/bonminOptProblem.h"
#endif
#ifdef GF2_WITH_GUROBI
//#   include "qcqpcpp/gurobiOptProblem.h"
#endif

#include "globfit2/util/diskUtil.hpp"
#include "globfit2/util/util.hpp"                     // timestamp2Str

#include "globfit2/primitives/pointPrimitive.h"
#include "globfit2/primitives/linePrimitive2.h"

#include "globfit2/io/io.h"
#include "globfit2/ground_truth/gtCreator.h"
#include "globfit2/optimization/candidateGenerator.h"
#include "globfit2/optimization/energyFunctors.h"     // PointLineDistanceFunctor,
#include "globfit2/optimization/problemSetup.h"     // everyPatchNeedsDirection()

namespace GF2
{

#if 0
//! \brief      Reads a candidates file, a cloud and displays them
//! \param argc Number of CLI arguments.
//! \param argv Vector of CLI arguments.
//! \return     EXIT_SUCCESS.
int
Solver::show( int argc, char** argv )
{
    if ( pcl::console::find_switch(argc,argv,"--help") || pcl::console::find_switch(argc,argv,"-h") )
    {
        std::cout << "[" << __func__ << "]: " << "Usage: gurobi_opt --show\n"
                  << "\t--dir \tthe directory containing the files to display\n"
                  << "\t--prims \tthe primitives file name in \"dir\"\n"
                  << "\t--cloud \tthe cloud file name in \"dir\"\n"
                  << "\t[--scale \talgorithm parameter]\n"
                  << "\t[--assoc \tpoint to line associations]\n"
                  << "\t[--no-rel \tdon't show perfect relationships as gray lines]"
                  << "\t[--no-clusters \tdon't show the \"ellipses\"]"
                  << std::endl;
        return EXIT_SUCCESS;
    }
    std::string dir;
    if ( pcl::console::parse_argument( argc, argv, "--dir", dir) < 0 )
    {
        std::cerr << "[" << __func__ << "]: " << "no directory specified by --dir ...exiting" << std::endl;
        return EXIT_FAILURE;
    }
    int err = EXIT_SUCCESS;

    std::string primitives_file;
    if ( pcl::console::parse_argument( argc, argv, "--prims", primitives_file) < 0 )
    {
        std::cerr << "[" << __func__ << "]: " << "no primitive file specified by --prims ...exiting" << std::endl;
        return EXIT_FAILURE;
    }

    PrimitiveContainerT lines;
    err = io::readPrimitives<PrimitiveT>( lines, dir + "/" + primitives_file );
    if ( EXIT_SUCCESS != err )
    {
        std::cerr << "[" << __func__ << "]: " << "failed to read " << dir + "/" + primitives_file << "...exiting" << std::endl;
        return err;
    }

    // read cloud
    PointContainerT points;
    std::string cloud_file;
    if ( pcl::console::parse_argument( argc, argv, "--cloud", cloud_file) < 0 )
    {
        std::cerr << "[" << __func__ << "]: " << "no cloud file specified by --cloud ...exiting" << std::endl;
        return EXIT_FAILURE;
    }

    // convert cloud
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud( new pcl::PointCloud<pcl::PointXYZRGB> );
    pcl::io::loadPLYFile( dir + "/" + cloud_file, *cloud );
    pclutil::cloudToVector<PointT::Allocator>( points, cloud );

    // parse associations

    std::string assoc_file;
    if ( pcl::console::parse_argument(argc,argv,"--assoc",assoc_file) > 0 )
    {
        std::vector<std::pair<int,int> > points_primitives;
        std::map<int,int>                linear_indices; // <pid,lid>
        io::readAssociations( points_primitives, dir + "/" + assoc_file, &linear_indices );

        for ( size_t pid = 0; pid != points_primitives.size(); ++pid )
        {
            if ( points_primitives[pid].first < static_cast<int>(points.size()) )
            {
                points[ pid ].setTag( PointT::GID, points_primitives[pid].first );
            }
            else
                std::cerr << "[" << __func__ << "]: " << "overindexed pid: " << pid << " >= " << points.size() << " points.size(), skipping..." << std::endl;
        }
    }

    // angles
    std::vector<Scalar> angles = { 0, M_PI_2, M_PI };

    // tags = point colours
    char use_tags = pcl::console::find_switch( argc, argv, "--use-tags" );
    if ( use_tags )
        use_tags += (char)pcl::console::find_switch( argc, argv, "--no-clusters" );

    // GF
    Scalar scale = 0.1;
    pcl::console::parse_argument( argc, argv, "--scale", scale );
    bool dont_show_rels = pcl::console::find_switch( argc, argv, "--no-rel" );

#warning "Remove this"
#if 0
    GF2::MyVisPtr vptr = Visualizer<PrimitiveContainerT,PointContainerT>::show<Scalar>( lines, points, scale
                                                                               , {1,0,0}
                                                                               , /*        spin: */ false
                                                                               , /* connections: */ dont_show_rels ? NULL : &angles
                                                                               , /*    show_ids: */ false
                                                                               , /*    use_tags: */ use_tags
                                                                               );


    std::string gf_out_path; // input_20140620_1130$ ../Release/bin/gurobi_opt --show --dir . --prims  primitives_1800.txt --cloud cloud_20140620_1130.ply --gf .
    if ( pcl::console::parse_argument( argc, argv, "--gf", gf_out_path) >= 0 )
    {
        std::cout << "[" << __func__ << "]: " << " optimizing GF to " << gf_out_path << std::endl;
        PrimitiveContainerT out_lines;
        GurobiOpt<Scalar,PrimitiveT,PointT>::globFit( out_lines
                                                      , lines
                                                      , points
                                                      , scale
                                                      );

    }

    vptr->spin();
#endif

    return EXIT_SUCCESS;
} // ... Solver::show()
#endif // Solver::show

//! \brief      Step 0. Takes an image, and samples it to a pointcloud. Saves points to img_path.parent_path/cloud.ply.
//! \param argc Number of CLI arguments.
//! \param argv Vector of CLI arguments.
//! \return     EXIT_SUCCESS.
int
Solver::sampleInput( int argc, char** argv )
{
    std::string img_path;
    bool valid_input = true;
    if ( (pcl::console::parse_argument(argc, argv, "--img"       , img_path  ) < 0) )
    {
        std::cerr << "[" << __func__ << "]: " << "--img is compulsory" << std::endl;
        valid_input = false;
    }

    int         n_points    = 200;
    float       scene_size  = 1.f,
                noise       = 0.015f,
                filter_size = 0.0075f;
    std::string out_dir = ".";
    pcl::console::parse_argument(argc, argv, "-N"           , n_points   );
    pcl::console::parse_argument(argc, argv, "--out_dir"    , out_dir    );
    pcl::console::parse_argument(argc, argv, "--scene-size" , scene_size );
    pcl::console::parse_argument(argc, argv, "--noise"      , noise      );
    pcl::console::parse_argument(argc, argv, "--filter-cell", filter_size);
    {
         std::cerr << "[" << __func__ << "]: " << "Usage:\t gurobi_opt --sample-input"
                   << " --img "          << img_path
                   << " [-N "            << n_points << "]"
                   << " [--out_dir "     << out_dir << "]"
                   << " [--scene-size "  << scene_size << "]"
                   << " [--noise "       << noise << "]"
                   << " [--filter-cell " << filter_size << "]"
                   << std::endl;

         if ( !valid_input )
             return EXIT_FAILURE;
    }

    // sample image
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud( new pcl::PointCloud<pcl::PointXYZRGB>() );
    GTCreator::sampleImage( cloud, img_path, n_points, noise, scene_size, filter_size, /* sensor_origin: */ NULL );

    // create output directory
    std::string img_name = boost::filesystem::path( img_path ).filename().stem().string();
    std::stringstream ss;
    ss << out_dir << "/" << img_name << "_noise_" << noise;
    std::string out_path = ss.str();
    if ( !boost::filesystem::exists(out_path) )
        boost::filesystem::create_directory( out_path );

    std::string cloud_path = out_path + "/cloud.ply";
    util::saveBackup( cloud_path );
    pcl::io::savePLYFileASCII( cloud_path, *cloud );
    std::cout << "[" << __func__ << "]: " << "saved " << cloud_path << std::endl;

    return EXIT_SUCCESS;
} // ...Solver::sampleInput()

//! \brief                  Step 1. Generates primitives from a cloud. Reads "cloud.ply" and saves "candidates.txt".
//! \param argc             Contains --cloud cloud.ply, and --scale scale.
//! \param argv             Contains --cloud cloud.ply, and --scale scale.
//! \return                 EXIT_SUCCESS.
int
Solver::generate(   int    argc
                  , char** argv )
{
    typedef typename PointContainerT::value_type PointPrimitiveT;

    CandidateGeneratorParams    generatorParams;
    std::string                 cloud_path              = "cloud.ply";
    Scalar                      scale                   = Scalar( 0.05 );
    Scalar                      angle_gen               = M_PI_2;
    std::string                 mode_string             = "representative_min";
    std::vector<std::string>    mode_opts               = { "full_min", "full_max", "squared_min", "representative_min" };
    std::string                 patch_refit_mode_string = "avg_dir";
    std::vector<std::string>    patch_refit_mode_opts   = { "spatial", "avg_dir" };

    // parse input
    {
        bool valid_input = true;
        if (    (pcl::console::parse_argument( argc, argv, "--cloud", cloud_path) < 0)
             || (pcl::console::parse_argument( argc, argv, "--scale", scale     ) < 0) )
        {
            std::cerr << "[" << __func__ << "]: " << "--cloud and --scale are compulsory" << std::endl;
            valid_input = false;
        }

        pcl::console::parse_argument( argc, argv, "--angle-limit", generatorParams.angle_limit );
        pcl::console::parse_argument( argc, argv, "--angle-limit-div", generatorParams.angle_limit_div );
        pcl::console::parse_argument( argc, argv, "--patch-dist-limit", generatorParams.patch_dist_limit ); // gets multiplied by scale
        pcl::console::parse_argument( argc, argv, "--mode", mode_string );
        pcl::console::parse_argument( argc, argv, "--patch-refit", patch_refit_mode_string );
        generatorParams.parseRefitMode( patch_refit_mode_string );
        pcl::console::parse_argument( argc, argv, "--angle-gen", angle_gen );
        pcl::console::parse_argument( argc, argv, "--patch-pop-limit", generatorParams.patch_population_limit );

        // print usage
        {
            std::cerr << "[" << __func__ << "]: " << "Usage:\t gurobi_opt --generate \n";
            std::cerr << "\t --cloud " << cloud_path << "\n";
            std::cerr << "\t --scale " << scale      << "\n";

            // linkage mode (full_min, full_max, squared_min)
            std::cerr << "\t [--mode *" << mode_string << "*\t";
            for ( size_t m = 0; m != mode_opts.size(); ++m )
                std::cerr << "|" << mode_opts[m];
            std::cerr << "]\n";

            // patch refit mode (spatial, avg_dir)
            std::cerr << "\t [--patch-refit *" << generatorParams.printRefitMode() << "*\t";
            for ( size_t m = 0; m != patch_refit_mode_opts.size(); ++m )
                std::cerr << "|" << patch_refit_mode_opts[m];
            std::cerr << "]\n";

            std::cerr << "\t [--angle-limit " << generatorParams.angle_limit << "]\n";
            std::cerr << "\t [--angle-limit-div " << generatorParams.angle_limit_div << "]\n";
            std::cerr << "\t [--patch-dist-limit " << generatorParams.patch_dist_limit << "]\n";
            std::cerr << "\t [--angle-gen " << angle_gen << "]\n";
            std::cerr << "\t [--patch-pop-limit " << generatorParams.patch_population_limit << "]\n";
            std::cerr << std::endl;

            if ( !valid_input || pcl::console::find_switch(argc,argv,"--help") || pcl::console::find_switch(argc,argv,"-h") )
                return EXIT_FAILURE;
        }

        if ( boost::filesystem::is_directory(cloud_path) )
        {
            cloud_path += "/cloud.ply";
        }

        if ( !boost::filesystem::exists(cloud_path) )
        {
            std::cerr << "[" << __func__ << "]: " << "cloud file does not exist! " << cloud_path << std::endl;
            return EXIT_FAILURE;
        }
    } // ... parse input

    // Read desired angles
    std::vector<Scalar> angles = { Scalar(0) };
    {
        // generate
        for ( Scalar angle = angle_gen; angle < M_PI; angle+= angle_gen )
            angles.push_back( angle );
        angles.push_back( M_PI );

        // print
        std::cout << "Desired angles: {";
        for ( size_t vi=0;vi!=angles.size();++vi)
            std::cout << angles[vi] << ((vi==angles.size()-1) ? "" : ", ");
        std::cout << "}\n";
    } // ... read angles

    // Read points
    PointContainerT     points;
    {
        io::readPoints<PointPrimitiveT>( points, cloud_path );
    }


    // WORK
    PrimitiveContainerT primitives;
    {

        // "full min": merge minimum largest angle between any two points, IF smallest spatial distance between points < scale * patch_dist_limit
        if ( !mode_string.compare("full_min") )
        {
            CandidateGenerator::generate< MyPrimitivePrimitiveAngleFunctor
                                        , FullLinkagePatchPatchDistanceFunctorT<Scalar,SpatialPatchPatchMinDistanceFunctorT<Scalar> >
                                        >( primitives, points, scale, angles, generatorParams );
        }
        // "full max": merge minimum largest angle between any two points, IF biggest spatial distance between points < scale * patch_dist_limit
        else if ( !mode_string.compare("full_max") ) // full_max
        {
            CandidateGenerator::generate< MyPrimitivePrimitiveAngleFunctor
                                        , FullLinkagePatchPatchDistanceFunctorT<Scalar,SpatialPatchPatchMaxDistanceFunctorT<Scalar> >
                                        >( primitives, points, scale, angles, generatorParams );
        }
        // "squared min": merge minimum, "max_angle^2 + (pi^2 / 4scale^2) * min_distance", IF smallest spatial distance between points < scale * patch_dist_limit. NOTE: use angle_thresh = 1..5
        else if ( !mode_string.compare("squared_min") ) // squared min
        {
            CandidateGenerator::generate< MyPrimitivePrimitiveAngleFunctor
                                        , SquaredPatchPatchDistanceFunctorT< Scalar,SpatialPatchPatchMinDistanceFunctorT<Scalar> >
                                        > ( primitives, points, scale, angles, generatorParams );
        }
        // "representative min:" merge closest representative angles, IF smallest spatial distance between points < scale * patch_dist_limit.
        else if ( !mode_string.compare("representative_min") )
        {
            //../Release/bin/gurobi_opt --generate --cloud . --scale 0.05 --angle-limit 0.01 --mode representative_min --patch-refit avg_dir --patch-dist-limit 0.5
            CandidateGenerator::generate< MyPrimitivePrimitiveAngleFunctor
                                        , RepresentativePatchPatchDistanceFunctorT< Scalar,SpatialPatchPatchMinDistanceFunctorT<Scalar> >
                                        > ( primitives, points, scale, angles, generatorParams );
        }
    } // ...work

    // Save point GID tags
    std::string candidates_path = boost::filesystem::path( cloud_path ).parent_path().string() + "/" + "candidates.txt";
    {
        int cnt_sync = 0, dir_gid_ex = 0;
        for ( size_t lid = 0; lid != primitives.size(); ++lid )
            for ( size_t lid1 = 0; lid1 != primitives[lid].size(); ++lid1 )
            {
                if ( primitives[lid][lid1].getTag( PrimitiveT::GID ) != static_cast<int>(lid) )
                    ++cnt_sync;
                if (   (primitives[lid][lid1].getTag( PrimitiveT::DIR_GID ) >= static_cast<int>(primitives.size()))
                   ||  (primitives[lid][lid1].getTag( PrimitiveT::DIR_GID ) <  static_cast<int>(0)                ) )
                    ++dir_gid_ex;
            }
        std::cout << "syncno: " << cnt_sync << ", dir_gid_exists_no: " << dir_gid_ex << std::endl;

        std::string f_assoc_path = boost::filesystem::path( cloud_path ).parent_path().string() + "/" + "points_primitives.txt";
        util::saveBackup( f_assoc_path );
        std::ofstream f_assoc( f_assoc_path );
        f_assoc << "# point_id,primitive_gid,primitive_dir_gid" << std::endl;
        for ( size_t pid = 0; pid != points.size(); ++pid )
        {
            f_assoc << pid
                       << "," << points[pid].getTag( PointPrimitiveT::GID )
                       << "," << -1  // assigned to patch, but no direction
                       << std::endl;
        }
        f_assoc.close();
        std::cout << "[" << __func__ << "]: " << "wrote to " << f_assoc_path << std::endl;
    }

    // backup
    util::saveBackup( candidates_path );
    return io::savePrimitives<PrimitiveT>( /* what: */ primitives, /* where_to: */ candidates_path );

    return 0;
} // ...Solver::generate()



//! \brief      Step 3. Reads a formulated problem from path and runs qcqpcpp::OptProblem::optimize() on it.
//! \param argc Number of command line arguments.
//! \param argv Vector of command line arguments.
//! \return     Exit code. 0 == EXIT_SUCCESS.
int
Solver::solve( int    argc
             , char** argv )
{
    int                                   err           = EXIT_SUCCESS;

    bool                                  verbose       = false;
    enum SOLVER { MOSEK, BONMIN, GUROBI } solver        = MOSEK;
    std::string                           project_path  = "problem", solver_str = "bonmin";
    Scalar                                max_time      = 360;
    int                                   bmode         = 0; // Bonmin solver mode, B_Bb by default
    std::string                           rel_out_path  = ".";


    // parse
    {
        bool valid_input = true;

        // verbose parsing
        if ( pcl::console::find_switch(argc,argv,"-v") || pcl::console::find_switch(argc,argv,"--verbose") )
            verbose = true;

        // solver parsing
        //std::string solver_str = "bonmin";
        valid_input &= pcl::console::parse_argument( argc, argv, "--solver", solver_str) >= 0;
        {
                 if ( !solver_str.compare("mosek")  ) solver = MOSEK;
            else if ( !solver_str.compare("bonmin") ) solver = BONMIN;
            else if ( !solver_str.compare("gurobi") ) solver = GUROBI;
            else
            {
                std::cerr << "[" << __func__ << "]: " << "Cannot parse solver " << solver_str << std::endl;
                valid_input = false;
            }
#           ifndef GF2_WITH_BONMIN
                 if ( solver == BONMIN )
                     throw new std::runtime_error("You have to specify a different solver, the project was not compiled with Bonmin enabled!");
#           endif // WITH_BONMIN
#           ifndef GF2_WITH_MOSEK
                 if ( solver == MOSEK )
                     throw new std::runtime_error("You have to specify a different solver, the project was not compiled with Mosek enabled!");
#           endif // WITH_MOSEK
#           ifndef GF2_WITH_GUROBI
                 if ( solver == GUROBI )
                     throw new std::runtime_error("You have to specify a different solver, the project was not compiled with GUROBI enabled!");
#           endif // WITH_GUROBI
        }
        // problem parsing
        pcl::console::parse_argument( argc, argv, "--problem", project_path );
        // parse time
        pcl::console::parse_argument( argc, argv, "--time", max_time );
        // parse bonmin solver mode
        pcl::console::parse_argument( argc, argv, "--bmode", bmode );
        pcl::console::parse_argument( argc, argv, "--rod"  , rel_out_path );

        // usage print
        std::cerr << "[" << __func__ << "]: " << "Usage:\t gurobi_opt\n"
                  << "\t--solver *" << solver_str << "* (mosek | bonmin | gurobi)\n"
                  << "\t--problem " << project_path << "\n"
                  << "\t[--time] " << max_time << "\n"
                  << "\t[--bmode *" << bmode << "*\n"
                         << "\t\t0 = B_BB, Bonmin\'s Branch-and-bound \n"
                         << "\t\t1 = B_OA, Bonmin\'s Outer Approximation Decomposition\n"
                         << "\t\t2 = B_QG, Bonmin's Quesada & Grossmann branch-and-cut\n"
                         << "\t\t3 = B_Hyb Bonmin's hybrid outer approximation\n"
                         << "\t\t4 = B_Ecp Bonmin's implemantation of ecp cuts based branch-and-cut a la FilMINT\n"
                         << "\t\t5 = B_IFP Bonmin's implemantation of iterated feasibility pump for MINLP]\n"
                  << "\t[--verbose] " << "\n"
                  << "\t[--rod " << rel_out_path << "]\t\t Relative output directory\n"
                  << "\t[--help, -h] "
                  << std::endl;

        // valid_input
        if ( !valid_input || pcl::console::find_switch(argc,argv,"--help") || pcl::console::find_switch(argc,argv,"-h") )
        {
            std::cerr << "[" << __func__ << "]: " << "--solver is compulsory" << std::endl;
            err = EXIT_FAILURE;
        } //...if valid_input
    } //...parse

    // select solver
    typedef double OptScalar; // Mosek, and Bonmin uses double internally, so that's what we have to do...
    typedef qcqpcpp::OptProblem<OptScalar> OptProblemT;
    OptProblemT *p_problem = NULL;
    if ( EXIT_SUCCESS == err )
    {
        switch ( solver )
        {
            case MOSEK:
                p_problem = new qcqpcpp::MosekOpt<OptScalar>( /* env: */ NULL );
                break;
            case BONMIN:
                p_problem = new qcqpcpp::BonminOpt<OptScalar>();
                break;

            default:
                std::cerr << "[" << __func__ << "]: " << "Unrecognized solver type, exiting" << std::endl;
                err = EXIT_FAILURE;
                break;
        } //...switch
    } //...select solver

    // problem.read()
    if ( EXIT_SUCCESS == err )
    {
        err += p_problem->read( project_path );
        if ( EXIT_SUCCESS != err )
            std::cerr << "[" << __func__ << "]: " << "Could not read problem, exiting" << std::endl;
    } //...problem.read()

    // problem.parametrize()
    {
        if ( max_time > 0 )
            p_problem->setTimeLimit( max_time );
        if ( solver == BONMIN )
        {
#           ifdef WITH_BONMIN
            static_cast<qcqpcpp::BonminOpt<OptScalar>*>(p_problem)->setAlgorithm( Bonmin::Algorithm(bmode) );
#           endif // WITH_BONMIN
        }
    }

    // problem.update()
    OptProblemT::ReturnType r = 0;
    if ( EXIT_SUCCESS == err )
    {
        // log
        if ( verbose ) { std::cout << "[" << __func__ << "]: " << "calling problem update..."; fflush(stdout); }

        // update
        r = p_problem->update();

        // check output
        if ( r != MSK_RES_OK )
        {
            std::cerr << "[" << __func__ << "]: " << "ooo...update didn't work with code " << r << std::endl;
            err = EXIT_FAILURE;
        }

        // log
        if ( verbose ) { std::cout << "[" << __func__ << "]: " << "problem update finished\n"; fflush(stdout); }
    } //...problem.update()

    // problem.optimize()
    if ( EXIT_SUCCESS == err )
    {
        // optimize
        std::vector<OptScalar> x_out;
        if ( r == p_problem->getOkCode() )
        {
            // log
            if ( verbose ) { std::cout << "[" << __func__ << "]: " << "calling problem optimize...\n"; fflush(stdout); }

            // work
            r = p_problem->optimize( &x_out, OptProblemT::OBJ_SENSE::MINIMIZE );

            // check output
            if ( r != p_problem->getOkCode() )
            {
                std::cerr << "[" << __func__ << "]: " << "ooo...optimize didn't work with code " << r << std::endl; fflush(stderr);
                err = r;
            }
        } //...optimize

        // copy output
        std::vector<Scalar> scalar_x_out( x_out.size() );
        if ( EXIT_SUCCESS == err )
        {
            // copy
            std::copy( x_out.begin(), x_out.end(), scalar_x_out.begin() );

            // print energy
//            checkSolution( scalar_x_out
//                           , p_problem->getLinObjectivesMatrix().cast<Scalar>()
//                           , p_problem->getQuadraticObjectivesMatrix().cast<Scalar>()
//                           , p_problem->getLinConstraintsMatrix().cast<Scalar>()
//                           , weights );
        } //...copy output

        // dump
        {
            {
                std::string x_path = project_path + "/x.csv";
                OptProblemT::SparseMatrix sp_x( x_out.size(), 1 ); // output colvector
                for ( size_t i = 0; i != x_out.size(); ++i )
                {
                    if ( int(round(x_out[i])) > 0 )
                    {
                        sp_x.insert(i,0) = x_out[i];
                    }
                }
                qcqpcpp::io::writeSparseMatrix<OptScalar>( sp_x, x_path, 0 );
                std::cout << "[" << __func__ << "]: " << "wrote output to " << x_path << std::endl;
            }

            std::string candidates_path;
            if ( pcl::console::parse_argument( argc, argv, "--candidates", candidates_path ) >= 0 )
            {
                // read primitives
                PrimitiveContainerT prims;
                {
                    if ( verbose ) std::cout << "[" << __func__ << "]: " << "reading primitives from " << candidates_path << "...";
                    io::readPrimitives<PrimitiveT>( prims, candidates_path );
                    if ( verbose ) std::cout << "reading primitives ok\n";
                } //...read primitives

                // save selected primitives
                PrimitiveContainerT out_prims( 1 );
                int prim_id = 0;
                for ( size_t l = 0; l != prims.size(); ++l )
                    for ( size_t l1 = 0; l1 != prims[l].size(); ++l1, ++prim_id )
                    {
                        if ( int(round(x_out[prim_id])) > 0 )
                        {
                            std::cout << "saving " << l << ", " << l1 << ", X: " << x_out[prim_id] << std::endl;
                            out_prims.back().push_back( prims[l][l1] );
                        }
                    } // ... for l1

                std::string parent_path = boost::filesystem::path(candidates_path).parent_path().string();
                if ( parent_path.empty() )  parent_path = "./";
                else                        parent_path += "/";

                std::string out_prim_path = parent_path + rel_out_path + "/primitives." + solver_str + ".txt";
                util::saveBackup    ( out_prim_path );
                io::savePrimitives<PrimitiveT>  ( out_prims, out_prim_path, /* verbose: */ true );
            } // if --candidates
            else
            {
                std::cout << "[" << __func__ << "]: " << "You didn't provide candidates, could not save primitives" << std::endl;
            } // it no --candidates
        }

    } //...problem.optimize()

    if ( p_problem ) { delete p_problem; p_problem = NULL; }

    return err;
}

//! \brief Unfinished function. Supposed to do GlobFit.
int
Solver::datafit( int    argc
               , char** argv )
{
    Scalar                  scale           = 0.05f;
    std::string             cloud_path      = "cloud.ply",
                            primitives_path = "candidates.txt";
    Scalar                  angle_gen       = M_PI_2;
    bool                    verbose         = false;

    // parse params
    {
        bool valid_input = true;
        valid_input &= pcl::console::parse_argument( argc, argv, "--scale"     , scale          ) >= 0;
        valid_input &= pcl::console::parse_argument( argc, argv, "--cloud"     , cloud_path     ) >= 0;
        valid_input &= pcl::console::parse_argument( argc, argv, "--primitives", primitives_path) >= 0;
        pcl::console::parse_argument( argc, argv, "--angle-gen", angle_gen );
        if ( pcl::console::find_switch(argc,argv,"-v") || pcl::console::find_switch(argc,argv,"--verbose") )
            verbose = true;

        std::cerr << "[" << __func__ << "]: " << "Usage:\t gurobi_opt --gfit\n"
                  << "\t--scale " << scale << "\n"
                  << "\t--cloud " << cloud_path << "\n"
                  << "\t--primitives " << primitives_path << "\n"
                  << "\t[--angle-gen " << angle_gen << "\n"
                  << std::endl;

        if ( !valid_input || pcl::console::find_switch(argc,argv,"--help") || pcl::console::find_switch(argc,argv,"-h") )
        {
            std::cerr << "[" << __func__ << "]: " << "--scale, --cloud and --candidates are compulsory" << std::endl;
            return EXIT_FAILURE;
        }
    } // ... parse params
    // read points
    PointContainerT     points;
    {
        if ( verbose ) std::cout << "[" << __func__ << "]: " << "reading cloud from " << cloud_path << "...";
        io::readPoints<PointT>( points, cloud_path );
        if ( verbose ) std::cout << "reading cloud ok\n";
    } //...read points

    // read primitives
    PrimitiveContainerT prims;
    {
        if ( verbose ) std::cout << "[" << __func__ << "]: " << "reading primitives from " << primitives_path << "...";
        io::readPrimitives<PrimitiveT>( prims, primitives_path );
        if ( verbose ) std::cout << "reading primitives ok\n";
    } //...read primitives

    // Read desired angles
    std::vector<Scalar> angles = { Scalar(0) };
    {
        for ( Scalar angle = angle_gen; angle < M_PI; angle+= angle_gen )
            angles.push_back( angle );
        angles.push_back( M_PI );
        std::cout << "Desired angles: {";
        for ( size_t vi=0;vi!=angles.size();++vi)
            std::cout << angles[vi] << ((vi==angles.size()-1) ? "" : ", ");
        std::cout << "}\n";
    } // ... read angles

#if 0
    // WORK
    {
        const int Dim = 2; // 2: nx,ny, 3: +nz
        typedef typename PrimitiveContainerT::value_type::value_type PrimitiveT;        // LinePrimitive or PlanePrimitive
        typedef typename PointContainerT::value_type                 PointPrimitiveT;   // PointPrimitive to store oriented points
        bool gurobi_log = false;

        std::vector<std::pair<std::string,Scalar> >     starting_values;    // [var_id] = < var_name, x0 >
        std::vector< std::vector<std::pair<int,int> > > points_primitives;  // each point is associated to a few { <lid,lid1>_0, <lid,lid1>_1 ... } primitives
        std::map< std::pair<int,int>, int >             prims_vars;         // < <lid,lid1>, var_id >, associates primitives to variables
        char                                            name[255];          // variable name
        const Eigen::Matrix<Scalar,3,1> origin( Eigen::Matrix<Scalar,3,1>::Zero() ); // to calculate d

        // add costs
        {
            for ( size_t lid = 0; lid != prims.size(); ++lid )
            {
                for ( size_t lid1 = 0; lid1 != prims[lid].size(); ++lid1 )
                {
                    // add var
                    const int gid     = prims[lid][lid1].getTag( PrimitiveT::GID     ); //lid;
                    const int dir_gid = prims[lid][lid1].getTag( PrimitiveT::DIR_GID ); //lid1;

                    Eigen::Matrix<Scalar,3,1> normal = prims[lid][lid1].template normal<Scalar>();
                    std::cout << "line_" << gid << "_" << dir_gid << ".n = " << normal.transpose() << std::endl;
                    vars[lid].emplace_back( std::vector<GRBVar>() );

                    sprintf( name, "nx_%d_%d", gid, dir_gid );
                    //vars[lid].back().emplace_back( model.addVar(-GRB_INFINITY, GRB_INFINITY, normal(0), GRB_CONTINUOUS, name) );
                    vars[lid].back().emplace_back( model.addVar(-GRB_INFINITY, GRB_INFINITY, 0.0, GRB_CONTINUOUS, name) );
                    starting_values.push_back( std::pair<std::string,Scalar>(name,normal(0)) );


                    sprintf( name, "ny_%d_%d", gid, dir_gid );
                    vars[lid].back().emplace_back( model.addVar(-GRB_INFINITY, GRB_INFINITY, 0.0, GRB_CONTINUOUS, name) );
                    starting_values.push_back( std::pair<std::string,Scalar>(name,normal(1)) );
                    //vars[lid].back().emplace_back( model.addVar(-GRB_INFINITY, GRB_INFINITY, normal(1), GRB_CONTINUOUS, name) );

                    if ( Dim > 2 )
                    {
                        sprintf( name, "nz_%d_%d", gid, dir_gid );
                        //vars[lid].back().emplace_back( model.addVar(-GRB_INFINITY, GRB_INFINITY, normal(2), GRB_CONTINUOUS, name) );
                        vars[lid].back().emplace_back( model.addVar(-GRB_INFINITY, GRB_INFINITY, 0.0, GRB_CONTINUOUS, name) );
                        starting_values.push_back( std::pair<std::string,Scalar>(name,normal(2)) );
                    }

                    sprintf( name, "d_%d_%d", gid, dir_gid );
                    Scalar d = Scalar(-1) * prims[lid][lid1].point3Distance(origin);
                    std::cout << "line_" << gid << "_" << dir_gid << ".d = " << d << std::endl;
                    // vars[lid].back().emplace_back( model.addVar(-GRB_INFINITY, GRB_INFINITY, lines[lid][lid1].point3Distance(origin), GRB_CONTINUOUS, name) );
                    vars[lid].back().emplace_back( model.addVar(-GRB_INFINITY, GRB_INFINITY, 0.0, GRB_CONTINUOUS, name) );
                    starting_values.push_back( std::pair<std::string,Scalar>(name,d) );
                }
            }

            // |normal|^2 = 1
            {
                char cname[64];
                for ( size_t lid = 0; lid != prims.size(); ++lid )
                {
                    for ( size_t lid1 = 0; lid1 != prims[lid].size(); ++lid1 )
                    {
                        GRBQuadExpr norm_constraint;
                        for ( int d = 0; d != Dim; ++d )
                            norm_constraint.addTerm( Scalar(1), vars[lid][lid1][d], vars[lid][lid1][d] );

                        sprintf( cname, "%lu_%lu_norm_ge", lid, lid1 );
                        model.addQConstr( norm_constraint, GRB_EQUAL, 1, cname );
                    }
                }
            }
            model.update();

            /// cost -> objective
            // assignments

            if ( points_primitives.size() )
                std::cerr << "[" << __func__ << "]: " << "warning, points_primitives not empty" << std::endl;

            // parse input associations instead of currently detecting point->primitive assocs
            {
                int max_group_id = -1;
                for ( size_t pid = 0; pid != points.size(); ++pid )
                    max_group_id = std::max( max_group_id, points[pid].getTag(PointPrimitiveT::GID) );
                std::cout << "[" << __func__ << "]: " << "max_group_id: " << max_group_id << std::endl;
                if ( max_group_id > 0 )
                {
                    points_primitives.resize( points.size() );
                    for ( size_t pid = 0; pid != points.size(); ++pid )
                    {
                        // assume linear...TODO: 2d
                        int tag = points[pid].getTag(PointPrimitiveT::GID);
                        if ( tag >= 0 )
                            points_primitives[pid].push_back( std::pair<int,int>(0,tag) );
                    }
                }
            }

            if ( !points_primitives.size() )
            {
                points_primitives.resize( points.size() );
                for ( size_t lid = 0; lid != prims.size(); ++lid )
                {
                    for ( size_t lid1 = 0; lid1 != prims[lid].size(); ++lid1 )
                    {
                        Scalar dist = Scalar(0);
                        for ( size_t pid = 0; pid != points.size(); ++pid )
                        {
                            // if within scale, add point constraint to line
                            dist = PointPrimitiveDistanceFunctorT::eval<Scalar>( points[pid], prims[lid][lid1] );
                            if ( dist < scale )
                            {
                                points_primitives[pid].push_back( std::pair<int,int>(lid,lid1) );
                                if ( lid1 == 2 && points_primitives[pid].size() > 1 )
                                {
                                    std::cout<<"[" << __func__ << "]: " << "oooo, points_primitives["<<pid<<"]:";
                                    for ( size_t vi = 0; vi!= points_primitives[pid].size(); ++vi )
                                        std::cout << "<" << points_primitives[pid][vi].first
                                                  << "," << points_primitives[pid][vi].second
                                                  << ">; ";
                                    std::cout << "\n";

                                }
                            }
                        } // ... for points
                    } // ... for lines
                } // ... for lines
            } // ... if points_primitives.empty()

            // add term
            Scalar coeff = Scalar(0);
            for ( size_t pid = 0; pid != points.size(); ++pid )
            {
                // skip, if ambiguous assignment
                if ( points_primitives[pid].size() != 1 )
                    continue;

                const int lid  = points_primitives[pid][0].first;
                const int lid1 = points_primitives[pid][0].second;

                // debug
                std::cout << "[" << __func__ << "]: " << "adding pid " << pid << " -> " << "lines[" << lid << "][" << lid1 << "]" << std::endl;

                // x,y,z
                for ( int dim = 0; dim != Dim; ++dim )
                {
                    // p_x^2 . n_x^2
                    coeff = ((typename PointPrimitiveT::VectorType)points[pid])(dim);
                    coeff *= coeff;
                    objectiveQExpr.addTerm( coeff, vars[lid][lid1][dim], vars[lid][lid1][dim] );
                    if ( gurobi_log ) std::cout << "added qterm(pid: " << pid << "): "
                                                << coeff << " * "
                                                << vars[lid][lid1][dim].get(GRB_StringAttr_VarName) << " * "
                                                << vars[lid][lid1][dim].get(GRB_StringAttr_VarName)
                                                << std::endl;

                    // 2 . p_x . n_x . d
                    coeff = Scalar(2) * ((typename PointPrimitiveT::VectorType)points[pid])(dim);
                    objectiveQExpr.addTerm( coeff, vars[lid][lid1][dim], vars[lid][lid1].back() ); // back == d
                    if ( gurobi_log ) std::cout << "added qterm(pid: " << pid << "): "
                                                << coeff << " * "
                                                << vars[lid][lid1][dim].get(GRB_StringAttr_VarName) << " * "
                                                << vars[lid][lid1].back().get(GRB_StringAttr_VarName)
                                                << std::endl;
                }

                // d^2
                coeff = Scalar(1);
                objectiveQExpr.addTerm( coeff, vars[lid][lid1].back(), vars[lid][lid1].back() );
                if ( gurobi_log ) std::cout << "added qterm(pid: " << pid << "): "
                                            << coeff << " * "
                                            << vars[lid][lid1].back().get(GRB_StringAttr_VarName) << " * "
                                            << vars[lid][lid1].back().get(GRB_StringAttr_VarName)
                                            << std::endl;

                // 2 . px . py . nx . ny
                coeff = Scalar(2) * ((typename PointPrimitiveT::VectorType)points[pid])(0) * ((typename PointPrimitiveT::VectorType)points[pid])(1);
                objectiveQExpr.addTerm( coeff, vars[lid][lid1][0], vars[lid][lid1][1] );
                if ( gurobi_log ) std::cout << "added qterm(pid: " << pid << "): "
                                            << coeff << " * "
                                            << vars[lid][lid1][0].get(GRB_StringAttr_VarName) << " * "
                                            << vars[lid][lid1][1].get(GRB_StringAttr_VarName)
                                            << std::endl;

            } // for points
            model.update();

            // set objective
            model.setObjective( objectiveQExpr, GRB_MINIMIZE );
            model.update();
        } //...add costs


        // dump
        {
            // dump x0 (starting values)
            {
                std::string f_start_path = params.store_path + "/" + "starting_values.m";
                ofstream f_start( f_start_path );
                f_start << "x0 = [ ...\n";
                for ( size_t i = 0; i != starting_values.size(); ++i )
                {
                    f_start << "    " << starting_values[i].second << ",      % " << starting_values[i].first << "\n";
                }
                f_start << "];";
                f_start.close();
                std::cout << "[" << __func__ << "]: " << "wrote to " << f_start_path << std::endl;
            } //...dump x0

            // dump associations
            {
                // reverse map for sorted output
                std::vector< std::vector< std::vector<int> > > primitives_points( prims.size() );
                for ( size_t pid = 0; pid != points_primitives.size(); ++pid )
                {
                    if ( points_primitives[pid].size() == 1 )
                    {
                        const int lid  = points_primitives[pid][0].first;
                        const int lid1 = points_primitives[pid][0].second;
                        if ( primitives_points[lid].size() <= lid1 )
                            primitives_points[ lid ].resize( lid1 + 1 );
                        primitives_points[ lid ][ lid1 ].push_back( pid );
                        std::cout << "added " << "primitives_points[ "<<lid<<" ][ "<<lid1<<" ].back(): " << primitives_points[ lid ][ lid1 ].back() << std::endl;
                    }
                    else
                    {
                        std::cout << "skipping pid " << pid << ", since: ";
                        std::cout<<"points_primitives["<<pid<<"]:";
                        for(size_t vi=0;vi!=points_primitives[pid].size();++vi)std::cout<<points_primitives[pid][vi].first<<","
                                                                                       << ";" << points_primitives[pid][vi].second << ";  ";
                        std::cout << "\n";
                    }
                }

                // dump associations
                std::string f_assoc_path = params.store_path + "/" + "points_primitives.txt";
                if ( !boost::filesystem::exists(f_assoc_path) )
                {
                    ofstream f_assoc( f_assoc_path );
                    f_assoc << "# point_id,primitive_pos_id,primitive_dir_id" << std::endl;
                    for ( size_t lid = 0; lid != primitives_points.size(); ++lid )
                        for ( size_t lid1 = 0; lid1 != primitives_points[lid].size(); ++lid1 )
                            for ( size_t pid_id = 0; pid_id != primitives_points[lid][lid1].size(); ++pid_id )
                            {
                                f_assoc << primitives_points[lid][lid1][pid_id]
                                           << "," << lid
                                           << "," << lid1 << std::endl;
                            }
                    f_assoc.close();
                    std::cout << "[" << __func__ << "]: " << "wrote to " << f_assoc_path << std::endl;
                }
                else
                {
                    std::cout << "[" << __func__ << "]: " << "did NOT write to " << f_assoc_path << ", since already exists" << std::endl;
                } //...if assoc_path exists
            } //...dump assoc
        } //...dump
    } //...work
#endif
    return 0;
} // ...Solver::datafit()

#if 0
//! \brief \deprecated Old optimization setup for Gurobi solution.
int
Solver::run( std::string                   img_path
             , Scalar               const  scale
             , std::vector<Scalar>  const& angles
             , int                         argc
             , char**                      argv     )
{
    PrimitiveContainerT lines;
    PointContainerT     points;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud;
    Scalar              noise = 0.f;

    // parse args
    SolverParams    solverParams;
    GurobiOptParams gurobiOptParams;
    CandidateGeneratorParams generatorParams;
    {
        pcl::console::parse_argument( argc, argv, "-N"                  , solverParams.n_points );

        generatorParams.show_fit_lines  = pcl::console::find_switch( argc, argv, "--show-fit-lines");
        generatorParams.show_candidates = pcl::console::find_switch( argc, argv, "--show-candidates");
        //            pcl::console::parse_argument( argc, argv, "--angle-limit"       , generatorParams.angle_limit );
        //            pcl::console::parse_argument( argc, argv, "--dist-limit-mult"   , generatorParams.dist_limit_scale_multiplier );

        pcl::console::parse_argument( argc, argv, "--unary"             , gurobiOptParams.unary_weight );
        pcl::console::parse_argument( argc, argv, "--pw"                , gurobiOptParams.pairwise_weight );
        pcl::console::parse_argument( argc, argv, "--complexity"        , gurobiOptParams.complexity_weight );
        pcl::console::parse_argument( argc, argv, "--time-limit"        , gurobiOptParams.time_limit      );
        pcl::console::parse_argument( argc, argv, "--time-chunk"        , gurobiOptParams.time_chunk      );
        pcl::console::parse_argument( argc, argv, "--threads"           , gurobiOptParams.thread_count    );
        pcl::console::parse_argument( argc, argv, "--noise"             , noise    );

        if      ( pcl::console::find_switch( argc, argv, "--binary")         ) gurobiOptParams.continuous = GRB_BINARY;
    }
    std::cout << "[" << __func__ << "]: " << "solver runs with " << solverParams.n_points << std::endl;

    // generate input
    std::string cloud_path, candidates_path;
    if ( (pcl::console::parse_argument( argc, argv, "--cloud"     , cloud_path     ) >= 0)
         && (pcl::console::parse_argument( argc, argv, "--candidates", candidates_path) >= 0) )
    {
        std::cout << "[" << __func__ << "]: " << "Gurobi not resampling image" << std::endl;
        // read points
        io::readPoints<PointT>( points, cloud_path, &cloud );

        // primitives
        io::readPrimitives<PrimitiveT>( lines, candidates_path );
    }
    else if ( !img_path.empty() )
    {
        // points
        {
            // sample image
            if ( !cloud ) cloud.reset( new pcl::PointCloud<pcl::PointXYZRGB>() );
            GTCreator::sampleImage( cloud, img_path, solverParams.n_points, noise /*scale/10.f*/, 2.f, 0.0075f );

            // convert to raw vector
            std::vector<PointT::VectorType> raw_points;
            //! \todo exchange to PointPrimitiveT::template toCloud<CloudXYZ::Ptr, _PointContainerT, PCLPointAllocator<PointPrimitiveT::Dim> >( cloud, points );
            pclutil::cloudToVector<PointT::RawAllocator>( raw_points, cloud );

            // convert to tagged vector
            points.reserve( raw_points.size() );
            for ( size_t pid = 0; pid != raw_points.size(); ++pid )
            {
                points.emplace_back( PointT(raw_points[pid]) );
                points.back().setTag( PointT::PID, pid );
                points.back().setTag( PointT::GID, pid ); // no grouping right now
            }
        }

        // lines
        CandidateGenerator::generate< MyPrimitivePrimitiveAngleFunctor
                                    , FullLinkagePatchPatchDistanceFunctorT<Scalar, SpatialPatchPatchMinDistanceFunctorT<Scalar> > // Hybrid linkage (max angle, min distance)
                                    >( lines, points, scale, angles, generatorParams );

    }

    // vis small
    Visualizer<PrimitiveContainerT,PointContainerT>::show<Scalar>( lines, points, scale );

    // solve
    std::string timestamp_str   = util::timestamp2Str();
    std::string fname           = boost::filesystem::path( img_path ).stem().string();
    std::string store_dir       = fname + timestamp_str;
    if ( !cloud_path.empty() )
    {
        fname = boost::filesystem::path( cloud_path ).parent_path().string();
        store_dir = fname;
        if ( store_dir.empty() )
            store_dir = ".";
    }
    {
        int i = 0;
        while ( boost::filesystem::exists(store_dir) )
        {
            std::stringstream ss;
            ss << "_" << i;
            if ( !i )                    store_dir += ss.str();
            else                         store_dir.replace( store_dir.rfind("_"), ss.str().size(), ss.str() );
        }
        boost::filesystem::create_directory( store_dir );
        gurobiOptParams.store_path  = store_dir;
    }

    // dump input
    std::string lines_path = store_dir + "/" + "candidates.txt";
    io::savePrimitives<PrimitiveT>( lines, lines_path );
    // cloud
    {
        std::string cloud_name = store_dir + "/" + "cloud.ply";
        util::saveBackup( cloud_name );
        pcl::io::savePLYFileASCII( cloud_name, *cloud );
        std::cout << "[" << __func__ << "]: " << "saved " << cloud_name << std::endl;
    }

    PrimitiveContainerT out_lines;
    GurobiOpt<Scalar,PrimitiveT,PointT>::solve( out_lines
                                                , lines
                                                , points
                                                , scale
                                                , angles
                                                , /* verbose: */ false
                                                , /*  params: */ &gurobiOptParams );

    GF2::vis::MyVisPtr vptr = Visualizer<PrimitiveContainerT,PointContainerT>::show<Scalar>( lines, points, scale, {0,0,1}, false );
    Visualizer<PrimitiveContainerT,PointContainerT>::show<Scalar>( out_lines, points, scale, {1,0,0} );

    return EXIT_SUCCESS;
} // ... Solver::run()
#endif

//! \brief              Prints energy of solution in \p x using \p weights.
//! \param[in] x        A solution to calculate the energy of.
//! \param[in] weights  Problem weights used earlier. \todo Dump to disk together with solution.
Eigen::Matrix<Solver::Scalar,3,1>
Solver::checkSolution( std::vector<Scalar> const& x
                     , Solver::SparseMatrix const& linObj
                     , Solver::SparseMatrix const& Qo
                     , Solver::SparseMatrix const& /* A */
                     , Eigen::Matrix<Scalar,3,1> const& weights )
{
    Eigen::Matrix<Scalar,3,1> energy; energy.setZero();

    SparseMatrix complexity( x.size(), 1 );
    for ( size_t row = 0; row != x.size(); ++row )
        complexity.insert( row, 0 ) = weights(2);

    // X
    SparseMatrix mx( x.size(), 1 );
    for ( size_t i = 0; i != x.size(); ++i )
        mx.insert( i, 0 ) = x[i];

    SparseMatrix data   = linObj - complexity;

    // qo
    SparseMatrix e02 = mx.transpose() * linObj;
    std::cout << "[" << __func__ << "]: " << "qo * x = " << e02.coeffRef(0,0) << std::endl; fflush(stdout);

    // datacost
    SparseMatrix e0 = (mx.transpose() * data);
    energy(0) = e0.coeffRef(0,0);
    //std::cout << "[" << __func__ << "]: " << "data: " << energy(0) << std::endl; fflush(stdout);

    // Qo
    SparseMatrix e1 = mx.transpose() * Qo * mx;
    energy(1) = e1.coeffRef(0,0);
    //std::cout << "[" << __func__ << "]: " << "x' * Qo * x = pw = " << energy(1) << std::endl; fflush(stdout);

    // complexity
    SparseMatrix e2 = mx.transpose() * complexity;
    energy(2) = e2.coeffRef(0,0);
    //std::cout << "[" << __func__ << "]: " << "complx = " << energy(2) << std::endl; fflush(stdout);
    std::cout << "[" << __func__ << "]: " << std::setprecision(9) << energy(0) << " + " << energy(1) << " + " << energy(2) << " = " << energy.sum();
    std::cout                             << std::setprecision(9) << weights(0) << " * " << energy(0)/weights(0)
                                                                  << " + " << weights(1) << " * " << energy(1) / weights(1)
                                                                  << " + " << weights(2) << " * " << energy(2) / weights(2)
                                                                  << std::endl;

    return energy;
}

} // ... ns GF2

#endif // __GF2_SOLVER_H__