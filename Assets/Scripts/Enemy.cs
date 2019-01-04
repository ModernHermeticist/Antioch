using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class Enemy : MonoBehaviour
{
    private float speed = 0.25f;
    private float maxDamage = 20;
    private float maxHitPoints = 10;
    private float hitPoints = 10.0f;

    private Vector3 playerPos;

    private GameObject player;
    private Transform playerTransform;

	// Use this for initialization
	void Start ()
    {
        name = "Orc";
        player = GameObject.Find("Player");
        playerTransform = player.transform;
        GameManager.instance.SetEnemyObject(gameObject);
    }
	
	// Update is called once per frame
	void Update ()
    {
        float step = speed * Time.deltaTime;
        if (hitPoints <= 0)
        {
            enabled = false;
            GetComponent<Renderer>().enabled = false;
            hitPoints = maxHitPoints;
        }

        transform.position = Vector3.MoveTowards(transform.position, playerTransform.position, step);
	}

    public void TakeDamage(float damage)
    {
        hitPoints -= damage;
    }
}
