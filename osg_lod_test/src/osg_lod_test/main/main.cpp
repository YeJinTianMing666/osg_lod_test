#include <iostream>

#include <istream>

#include <osg/Group>
#include <osg/Notify>
#include <osg/Geometry>
#include <osg/ArgumentParser>
#include <osg/ApplicationUsage>
#include <osg/Texture2D>
#include <osg/Geode>
#include <osg/PagedLOD>

#include <osgDB/Registry>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osgDB/FileNameUtils>
#include <osgDB/FileUtils>

#include <osgUtil/Optimizer>

#include <iostream>
#include <sstream>

class NameVistor : public osg::NodeVisitor
{
public:
	NameVistor():
		osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN),
		_count(0)
	{
	}

	virtual void apply(osg::Node& node)
	{
		std::ostringstream os;
		os << node.className() << "_"<<_count++;

		node.setName(os.str());

		traverse(node);
	}    

	unsigned int _count;
};

class CheckVisitor : public osg::NodeVisitor
{
public:
	CheckVisitor():
		osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN)
	{
	}

	virtual void apply(osg::PagedLOD& plod)
	{
		std::cout<<"PagedLOD "<<plod.getName()<<"  numRanges = "<< plod.getNumRanges()<<"  numFiles = "<<plod.getNumFileNames()<<std::endl;
		for(unsigned int i=0;i<plod.getNumFileNames();++i)
		{
			std::cout<<"  files = '"<<plod.getFileName(i)<<"'"<<std::endl;
		}
	}    
};


class WriteOutPagedLODSubgraphsVistor : public osg::NodeVisitor
{
public:
	WriteOutPagedLODSubgraphsVistor():
		osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN)
	{
	}

	virtual void apply(osg::PagedLOD& plod)
	{

		// go through all the named children and write them out to disk.
		for(unsigned int i=0;i<plod.getNumChildren();++i)
		{
			osg::Node* child = plod.getChild(i);
			std::string filename = plod.getFileName(i);
			if (!filename.empty())
			{
				osg::notify(osg::NOTICE)<<"Writing out "<<filename<<std::endl;
				std::cout <<"Writing out "<<filename<<std::endl;
				osgDB::writeNodeFile(*child,filename);
			}
		}

		traverse(plod);
	}    
};

class ConvertToPageLODVistor : public osg::NodeVisitor
{
public:
	ConvertToPageLODVistor(const std::string& basename, const std::string& extension, bool makeAllChildrenPaged):
		osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN),
		_basename(basename),
		_extension(extension),
		_makeAllChildrenPaged(makeAllChildrenPaged)
	{
	}

	virtual ~ConvertToPageLODVistor()
	{
	}

	virtual void apply(osg::LOD& lod)
	{
		_lodSet.insert(&lod);

		traverse(lod);
	}    

	virtual void apply(osg::PagedLOD& plod)
	{
		// do thing, but want to avoid call LOD.
		traverse(plod);
	}

	void convert()
	{
		unsigned int lodNum = 0;
		for(LODSet::iterator itr = _lodSet.begin();
			itr != _lodSet.end();
			++itr, ++lodNum)
		{
			osg::ref_ptr<osg::LOD> lod = const_cast<osg::LOD*>(itr->get());

			if (lod->getNumParents()==0)
			{
				osg::notify(osg::NOTICE)<<"Warning can't operator on root node."<<std::endl;
				break;
			}

			if (!_makeAllChildrenPaged && lod->getNumRanges()<2)
			{
				osg::notify(osg::NOTICE)<<"Leaving LOD with one child as is."<<std::endl;
				break;
			}

			osg::notify(osg::NOTICE)<<"Converting LOD to PagedLOD."<<std::endl;

			osg::PagedLOD* plod = new osg::PagedLOD;

			const osg::LOD::RangeList& originalRangeList = lod->getRangeList();
			typedef std::multimap< osg::LOD::MinMaxPair , unsigned int > MinMaxPairMap;
			MinMaxPairMap rangeMap;
			unsigned int pos = 0;
			for(osg::LOD::RangeList::const_iterator ritr = originalRangeList.begin();
				ritr != originalRangeList.end();
				++ritr, ++pos)
			{
				rangeMap.insert(std::multimap< osg::LOD::MinMaxPair , unsigned int >::value_type(*ritr, pos));
			}

			pos = 0;
			for(MinMaxPairMap::reverse_iterator mitr = rangeMap.rbegin();
				mitr != rangeMap.rend();
				++mitr, ++pos)
			{
				if (pos==0 && !_makeAllChildrenPaged)
				{
					plod->addChild(lod->getChild(mitr->second), mitr->first.first, mitr->first.second);
				}
				else
				{
					std::string filename = _basename;
					std::ostringstream os;
					os << _basename << "_"<<lodNum<<"_"<<pos<<_extension;

					plod->addChild(lod->getChild(mitr->second), mitr->first.first, mitr->first.second, os.str());
				}
			}

			osg::Node::ParentList parents = lod->getParents();
			for(osg::Node::ParentList::iterator pitr=parents.begin();
				pitr!=parents.end();
				++pitr)
			{
				(*pitr)->replaceChild(lod.get(),plod);
			}

			plod->setCenter(plod->getBound().center());


		}
	}


	typedef std::set< osg::ref_ptr<osg::LOD> >  LODSet;
	LODSet _lodSet;
	std::string _basename;
	std::string _extension;
	bool _makeAllChildrenPaged;
};


int proxy_main_pagedlod_test( int argc, char **argv )
{

	// use an ArgumentParser object to manage the program arguments.
	osg::ArgumentParser arguments(&argc,argv);

	// set up the usage document, in case we need to print out how to use this program.
	arguments.getApplicationUsage()->setApplicationName(arguments.getApplicationName());
	arguments.getApplicationUsage()->setDescription(arguments.getApplicationName()+" creates a hierarchy of files for paging which can be later loaded by viewers.");
	arguments.getApplicationUsage()->setCommandLineUsage(arguments.getApplicationName()+" [options] filename ...");
	arguments.getApplicationUsage()->addCommandLineOption("-h or --help","Display this information");
	arguments.getApplicationUsage()->addCommandLineOption("-o","set the output file (defaults to output.ive)");
	arguments.getApplicationUsage()->addCommandLineOption("--makeAllChildrenPaged","Force all children of LOD to be written out as external PagedLOD children");

	// if user request help write it out to cout.
	if (arguments.read("-h") || arguments.read("--help"))
	{
		arguments.getApplicationUsage()->write(std::cout);
		return 1;
	}

	std::string outputfile("output.ive");
	while (arguments.read("-o",outputfile)) {}


	bool makeAllChildrenPaged = false;
	while (arguments.read("--makeAllChildrenPaged")) { makeAllChildrenPaged = true; }

	// any option left unread are converted into errors to write out later.
	arguments.reportRemainingOptionsAsUnrecognized();

	// report any errors if they have occurred when parsing the program arguments.
	if (arguments.errors())
	{
		arguments.writeErrorMessages(std::cout);
		return 1;
	}

	//     if (arguments.argc()<=1)
	//     {
	//         arguments.getApplicationUsage()->write(std::cout,osg::ApplicationUsage::COMMAND_LINE_OPTION);
	//         return 1;
	//     }


	osg::ref_ptr<osg::Node> model = osgDB::readNodeFiles(arguments);

	if (!model)
	{
		osg::notify(osg::NOTICE)<<"No model loaded."<<std::endl;
		return 1;
	}

	std::string basename( osgDB::getNameLessExtension(outputfile) );
	std::string ext = '.'+ osgDB::getFileExtension(outputfile);

	ConvertToPageLODVistor converter(basename,ext, makeAllChildrenPaged);
	model->accept(converter);
	converter.convert();

	NameVistor nameNodes;
	model->accept(nameNodes);

	CheckVisitor checkNodes;
	model->accept(checkNodes);

	if (model.valid())
	{
		osgDB::writeNodeFile(*model,outputfile);

		WriteOutPagedLODSubgraphsVistor woplsv;
		model->accept(woplsv);
	}

	return 0;
}

inline bool sphere_contained_most(const osg::BoundingSphere & main_sphere,
							 const osg::BoundingSphere & test_sphere)
{
	double max_dist_bt_centers = main_sphere.radius() - test_sphere.radius()/2.;
	if (max_dist_bt_centers >= 0 &&
		(main_sphere.center() - test_sphere.center()).length() <= max_dist_bt_centers)
		return true;
	else 
		return false;
}

int textured_mesh_segmentation()
{
	int ret = -1;

	do 
	{

		ret = 0;
	} while (0);
error0:

	return ret;
}

int convert_to_pagedlod(std::string obj_filename, std::string ive_filename)
{
	int ret = -1;

	do 
	{
		osg::ref_ptr<osg::PagedLOD> paged_lod = new osg::PagedLOD;
		paged_lod->addChild(osgDB::readNodeFile(obj_filename), 0, FLT_MAX);
		if (!osgDB::writeNodeFile(*paged_lod, ive_filename))
		{
			std::cout<< "PagedLOD " << ive_filename<<" write failed.."<<std::endl;
			break;
		}

		ret = 0;
	} while (0);
error0:

	return ret;
}

int process_config_file2(const std::string & config_filename,
						const std::string & out_dir,
						const std::string & output_ext)
{
	int ret = -1;

	do 
	{
		std::string lod_filename = out_dir + "\\out" + ".ive"/*output_ext*/;

		std::string top_level_filename;
		std::vector<std::string> level_directories;
		std::vector<std::string> pagedlod_children;
		std::string node_file_ext = "obj";


		// 读取config文件
		std::ifstream config_file(config_filename);
		if (!config_file.good()) break;

		std::string line;
		std::getline(config_file, line);
		if (!osgDB::fileExists(line) || osgDB::fileType(line) != osgDB::REGULAR_FILE) break;
		top_level_filename = line;

		while (config_file.good())
		{
			line.clear();
			std::getline(config_file, line);
			if (osgDB::fileExists(line) && osgDB::fileType(line) == osgDB::DIRECTORY) 
				level_directories.push_back(line);
		}


		// 每个阶层处理
		std::vector<osg::BoundingSphere> bounding_sphere_children;
		std::string level_ive_dir = out_dir + "\\ive";
		if (!osgDB::makeDirectory(level_ive_dir))
		{
			osg::notify(osg::NOTICE)<<"failed to create ive directory."<<std::endl;
			goto error0;
		}
		while (level_directories.size() > 0)
		{
			int level_index = level_directories.size();
			std::stringstream sstr;
			std::string tmp_index;
			sstr << level_index;
			sstr >> tmp_index;


			if (bounding_sphere_children.size() != pagedlod_children.size())
				goto error0;


			std::string level_dir = level_directories.back();

			osgDB::DirectoryContents dir_contents = osgDB::getDirectoryContents(level_dir);
			size_t num_content = dir_contents.size();
			std::vector<std::string> current_pagedlod_filename;
			std::vector<osg::BoundingSphere> current_bounding_spheres;
			for (int i_c = 0; i_c < num_content; ++i_c)
			{
				std::string content_name = level_dir + "\\"+dir_contents[i_c];
				if (osgDB::fileType(content_name) != osgDB::REGULAR_FILE ||
					osgDB::getFileExtension(content_name).compare(node_file_ext))
					continue;


				std::string output_pagedlod_name = level_ive_dir + 
					"\\" + tmp_index + "_" +
					osgDB::getNameLessExtension(osgDB::getSimpleFileName(content_name)) + output_ext;


				// convert to pagedlod node
				osg::ref_ptr<osg::PagedLOD> lod = new osg::PagedLOD;
				lod->addChild(osgDB::readNodeFile(content_name), 0, FLT_MAX);
				float radius = lod->getBound().radius() * 1.5;


				// add children if exists
				int num_added_children = 0;
				for (int i_ch = 0; i_ch < pagedlod_children.size(); ++i_ch)
				{	
					// if contained
					if (!sphere_contained_most(lod->getBound(), bounding_sphere_children[i_ch]))
						continue;

					// add as child
					//std::string rel_path = osgDB::getPathRelative(level_dir, pagedlod_children[i_ch]);	
					lod->setFileName(num_added_children + 1, /*rel_path*/osgDB::getSimpleFileName(pagedlod_children[i_ch]));
					lod->setRange(num_added_children + 1, 0, radius);

					++num_added_children;
				}				


				// save file
				radius = num_added_children > 0 ? radius : 0;
				lod->setRange(0, radius, FLT_MAX);
				lod->setCenter(lod->getBound().center());	
				if (!osgDB::writeNodeFile(*lod,output_pagedlod_name))
					std::cout<<output_pagedlod_name<<" write failed.."<<std::endl;


				// insert to children (filename and bounding sphere)
				current_pagedlod_filename.push_back(output_pagedlod_name);
				current_bounding_spheres.push_back(lod->getBound());
			}			


			pagedlod_children.swap(current_pagedlod_filename);
			bounding_sphere_children.swap(current_bounding_spheres);

			level_directories.pop_back();
		}


		// top level pagedlode
		osg::ref_ptr<osg::PagedLOD> lod = new osg::PagedLOD;
		lod->addChild(osgDB::readNodeFile(top_level_filename), 0, FLT_MAX);
		float top_level_radius = lod->getBound().radius() * 1.5;
		for (int i_ch = 0; i_ch < pagedlod_children.size(); ++i_ch)
		{			
			std::string rel_path = osgDB::getPathRelative(out_dir, pagedlod_children[i_ch]);	
			lod->setFileName(i_ch + 1, rel_path);
			lod->setRange(i_ch + 1, 0, top_level_radius);
		}
		lod->setRange(0, top_level_radius, FLT_MAX);
		lod->setCenter(lod->getBound().center());	
		if (!osgDB::writeNodeFile(*lod,lod_filename))
			std::cout<<lod_filename<<" write failed.."<<std::endl;


		ret = 0;
	} while (0);
error0:

	return ret;
}

int process_config_file(const std::string & config_filename,
						const std::string & out_dir,
						const std::string & output_ext)
{
	int ret = -1;

	do 
	{
		std::string lod_filename = out_dir + "\\out" + ".ive"/*output_ext*/;

		std::string top_level_filename;
		std::vector<std::string> level_directories;
		std::vector<std::string> pagedlod_children;
		std::string node_file_ext = "obj";


		// 读取config文件
		std::ifstream config_file(config_filename);
		if (!config_file.good()) break;

		std::string line;
		std::getline(config_file, line);
		if (!osgDB::fileExists(line) || osgDB::fileType(line) != osgDB::REGULAR_FILE) break;
		top_level_filename = line;

		while (config_file.good())
		{
			line.clear();
			std::getline(config_file, line);
			if (osgDB::fileExists(line) && osgDB::fileType(line) == osgDB::DIRECTORY) 
				level_directories.push_back(line);
		}


		// 每个阶层处理
		std::vector<osg::BoundingSphere> bounding_sphere_children;
		std::string level_ive_dir = out_dir + "\\ive";
		if (!osgDB::makeDirectory(level_ive_dir))
		{
			osg::notify(osg::NOTICE)<<"failed to create ive directory."<<std::endl;
			goto error0;
		}
		while (level_directories.size() > 0)
		{
			int level_index = level_directories.size();
			std::stringstream sstr;
			std::string tmp_index;
			sstr << level_index;
			sstr >> tmp_index;
// 			std::string output_level_dir = level_ive_dir + "\\" + tmp_index;
// 			if (!osgDB::makeDirectory(output_level_dir))
// 			{
// 				osg::notify(osg::NOTICE)<<"failed to create ive directory."<<std::endl;
// 				goto error0;
// 			}


			if (bounding_sphere_children.size() != pagedlod_children.size())
				goto error0;


			std::string level_dir = level_directories.back();
			
			osgDB::DirectoryContents dir_contents = osgDB::getDirectoryContents(level_dir);
			size_t num_content = dir_contents.size();
			std::vector<std::string> current_pagedlod_filename;
			std::vector<osg::BoundingSphere> current_bounding_spheres;
			for (int i_c = 0; i_c < num_content; ++i_c)
			{
				std::string content_name = level_dir + "\\"+dir_contents[i_c];
				if (osgDB::fileType(content_name) != osgDB::REGULAR_FILE ||
					osgDB::getFileExtension(content_name).compare(node_file_ext))
					continue;
				
				
				std::string output_pagedlod_name = level_ive_dir + 
					"\\" + tmp_index + "_" +
					osgDB::getNameLessExtension(osgDB::getSimpleFileName(content_name)) + output_ext;


				// convert to pagedlod node
				osg::ref_ptr<osg::PagedLOD> lod = new osg::PagedLOD;
				lod->addChild(osgDB::readNodeFile(content_name), 0, FLT_MAX);
				float radius = lod->getBound().radius() * 1.5;


				// add children if exists
				int num_added_children = 0;
				for (int i_ch = 0; i_ch < pagedlod_children.size(); ++i_ch)
				{	
					// if contained
					if (!sphere_contained_most(lod->getBound(), bounding_sphere_children[i_ch]))
						continue;
					
					// add as child
					//std::string rel_path = osgDB::getPathRelative(level_dir, pagedlod_children[i_ch]);	
					lod->setFileName(num_added_children + 1, /*rel_path*/osgDB::getSimpleFileName(pagedlod_children[i_ch]));
					lod->setRange(num_added_children + 1, 0, radius);

					++num_added_children;
				}				


				// save file
				radius = num_added_children > 0 ? radius : 0;
				lod->setRange(0, radius, FLT_MAX);
				lod->setCenter(lod->getBound().center());	
				if (!osgDB::writeNodeFile(*lod,output_pagedlod_name))
					std::cout<<output_pagedlod_name<<" write failed.."<<std::endl;


				// insert to children (filename and bounding sphere)
				current_pagedlod_filename.push_back(output_pagedlod_name);
				current_bounding_spheres.push_back(lod->getBound());
			}			
			

			pagedlod_children.swap(current_pagedlod_filename);
			bounding_sphere_children.swap(current_bounding_spheres);

			level_directories.pop_back();
		}


		// top level pagedlode
		osg::ref_ptr<osg::PagedLOD> lod = new osg::PagedLOD;
		lod->addChild(osgDB::readNodeFile(top_level_filename), 0, FLT_MAX);
		float top_level_radius = lod->getBound().radius() * 1.5;
		for (int i_ch = 0; i_ch < pagedlod_children.size(); ++i_ch)
		{			
			std::string rel_path = osgDB::getPathRelative(out_dir, pagedlod_children[i_ch]);	
			lod->setFileName(i_ch + 1, rel_path);
			lod->setRange(i_ch + 1, 0, top_level_radius);
		}
		lod->setRange(0, top_level_radius, FLT_MAX);
		lod->setCenter(lod->getBound().center());	
		if (!osgDB::writeNodeFile(*lod,lod_filename))
			std::cout<<lod_filename<<" write failed.."<<std::endl;


		ret = 0;
	} while (0);
error0:

	return ret;
}

int proxy_main_custom_test(int argc, char ** argv)
{
	// use an ArgumentParser object to manage the program arguments.
	osg::ArgumentParser arguments(&argc,argv);

	// set up the usage document, in case we need to print out how to use this program.
	arguments.getApplicationUsage()->setApplicationName(arguments.getApplicationName());
	arguments.getApplicationUsage()->setDescription(arguments.getApplicationName()+" blablablabla.");
	arguments.getApplicationUsage()->setCommandLineUsage(arguments.getApplicationName()+" [options] -dir directory ...");
	arguments.getApplicationUsage()->addCommandLineOption("-h or --help","Display this information");
	arguments.getApplicationUsage()->addCommandLineOption("-o","set the output directory");
	arguments.getApplicationUsage()->addCommandLineOption("-dir","set the input directory");
	arguments.getApplicationUsage()->addCommandLineOption("-config","set the config file.");

	if (arguments.read("-h") || arguments.read("--help") || argc < 3)
	{
		arguments.getApplicationUsage()->write(std::cout);
		return 1;
	}


	std::string output_ext(".ive");
	std::string lod_filename = "out" + output_ext;
	std::string out_dir("");
	std::string dir_name("");
	std::string config_file("");

	std::string level1_name;
	std::string level2_name;

	while (arguments.read("-o",out_dir)) {}
	if (!osgDB::makeDirectory(out_dir))
	{
		osg::notify(osg::NOTICE)<<"failed to create output directory."<<std::endl;
		return 1;
	}
	
	while (arguments.read("-dir",dir_name)) {}	

	while (arguments.read("-config",config_file)) {}

	// any option left unread are converted into errors to write out later.
	arguments.reportRemainingOptionsAsUnrecognized();

	// report any errors if they have occurred when parsing the program arguments.
	if (arguments.errors())
	{
		arguments.writeErrorMessages(std::cout);
		return 1;
	}

	if (!config_file.empty())
	{
		if (process_config_file2(config_file, out_dir, output_ext))
		{
			std::cout<<"process config file failed."<<std::endl;
			return 1;
		}
	} else {

		if (dir_name.empty())
		{
			osg::notify(osg::NOTICE)<<"no input directory."<<std::endl;
			return 1;
		}

		lod_filename = out_dir + "\\" + lod_filename;
		level1_name = dir_name + "\\l1.ive";
		level2_name = dir_name + "\\test.obj";
		float zoom_boundary = 8.; 

		std::string level_lod_dir = out_dir + "\\ive";
		if (!osgDB::makeDirectory(level_lod_dir))
		{
			osg::notify(osg::NOTICE)<<"failed to create ive directory."<<std::endl;
			return 1;
		}


		// 读取第一层
		osg::ref_ptr<osg::PagedLOD> lod = new osg::PagedLOD;
		lod->addChild(osgDB::readNodeFile(level1_name), 0, FLT_MAX);
		//std::cout<<lod->getBound().radius()<<std::endl;
		//lod->addChild(osgDB::readNodeFile(level2_name), 0., 15.f);


		// 把次层写成pagedlod格式并加入第一层
		std::string level2_paged_name( level_lod_dir + "\\level2" + output_ext );
		osg::ref_ptr<osg::PagedLOD> lod_2 = new osg::PagedLOD;
		lod_2->addChild(osgDB::readNodeFile(level2_name), 0, FLT_MAX);
		if (!osgDB::writeNodeFile(*lod_2, level2_paged_name))
			std::cout<<level2_paged_name<<" write failed.."<<std::endl;
		float radius = lod->getBound().radius();
		std::cout<<radius<<std::endl;
		std::string rel_path = osgDB::getPathRelative(out_dir, level2_paged_name);	
		lod->setFileName(1,/*level2_paged_name*/rel_path);
		lod->setRange(1, 0, /*zoom_boundary*/radius);
		lod->setRange(0, radius, FLT_MAX);


		// 写下lod
		lod->setCenter(lod->getBound().center());	
		if (!osgDB::writeNodeFile(*lod,lod_filename))
			std::cout<<out_dir<<" write failed.."<<std::endl;
	}

	return 0;
}


void main(int argc, char **argv)
{
	int ret = -1;

	//ret proxy_main_pagedlod_test(argc, argv);

	ret = proxy_main_custom_test(argc, argv);

	if (ret)
		std::cout<<"failed.."<<std::endl;
	else 
		std::cout<<"done."<<std::endl;

	std::cout<<"osg_lod_test."<<std::endl;
}