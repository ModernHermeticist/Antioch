using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class GameManager : MonoBehaviour
{
    public static GameManager instance = null;

    public GameObject gui;

    public Grid grid;

    public Player playerScript;

    public Enemy enemyScript;

    private GameObject enemyObject;


	// Use this for initialization
	void Awake ()
    {
		if (instance == null)
        {
            instance = this;
        }

        else if (instance != this)
        {
            Destroy(gameObject);
        }

        DontDestroyOnLoad(gameObject);

        //Instantiate(gui);

        Instantiate(grid);

        //Instantiate(playerScript);

        Instantiate(enemyScript);

    }
	
	// Update is called once per frame
	void Update ()
    {
		if (!enemyObject.GetComponent<Renderer>().enabled)
        {
            enemyObject.transform.position = ChooseSpawnNode();
            enemyObject.GetComponent<Renderer>().enabled = true;
            enemyObject.GetComponent<Enemy>().enabled = true;
        }
	}

    private Vector3 ChooseSpawnNode()
    {
        int n = Random.Range(0, 3);
        string nodeName = "SpawnPoint" + n;
        GameObject spawnNode = GameObject.Find(nodeName);
        return spawnNode.transform.position;
    }

    public void SetEnemyObject(GameObject objectToSetAs)
    {
        enemyObject = objectToSetAs;
    }
}
